#include <petsc/private/snesimpl.h> /*I "petscsnes.h" I*/
#include <petscdm.h>

#define H(i,j)  qn->dXdFmat[i*qn->m + j]

const char *const SNESQNScaleTypes[] =        {"DEFAULT","NONE","SCALAR","DIAGONAL","JACOBIAN","SNESQNScaleType","SNES_QN_SCALING_",NULL};
const char *const SNESQNRestartTypes[] =      {"DEFAULT","NONE","POWELL","PERIODIC","SNESQNRestartType","SNES_QN_RESTART_",NULL};
const char *const SNESQNTypes[] =             {"LBFGS","BROYDEN","BADBROYDEN","SNESQNType","SNES_QN_",NULL};

typedef struct {
  Mat               B;                    /* Quasi-Newton approximation Matrix (MATLMVM) */
  PetscInt          m;                    /* The number of kept previous steps */
  PetscReal         *lambda;              /* The line search history of the method */
  PetscBool         monflg;
  PetscViewer       monitor;
  PetscReal         powell_gamma;         /* Powell angle restart condition */
  PetscReal         scaling;              /* scaling of H0 */
  SNESQNType        type;                 /* the type of quasi-newton method used */
  SNESQNScaleType   scale_type;           /* the type of scaling used */
  SNESQNRestartType restart_type;         /* determine the frequency and type of restart conditions */
} SNES_QN;

static PetscErrorCode SNESSolve_QN(SNES snes)
{
  PetscErrorCode       ierr;
  SNES_QN              *qn = (SNES_QN*) snes->data;
  Vec                  X,Xold;
  Vec                  F,W;
  Vec                  Y,D,Dold;
  PetscInt             i, i_r;
  PetscReal            fnorm,xnorm,ynorm,gnorm;
  SNESLineSearchReason lssucceed;
  PetscBool            badstep,powell,periodic;
  PetscScalar          DolddotD,DolddotDold;
  SNESConvergedReason  reason;

  /* basically just a regular newton's method except for the application of the Jacobian */

  PetscFunctionBegin;
  if (snes->xl || snes->xu || snes->ops->computevariablebounds) SETERRQ(PetscObjectComm((PetscObject)snes),PETSC_ERR_ARG_WRONGSTATE, "SNES solver %s does not support bounds", ((PetscObject)snes)->type_name);

  ierr = PetscCitationsRegister(SNESCitation,&SNEScite);CHKERRQ(ierr);
  F    = snes->vec_func;                /* residual vector */
  Y    = snes->vec_sol_update;          /* search direction generated by J^-1D*/
  W    = snes->work[3];
  X    = snes->vec_sol;                 /* solution vector */
  Xold = snes->work[0];

  /* directions generated by the preconditioned problem with F_pre = F or x - M(x, b) */
  D    = snes->work[1];
  Dold = snes->work[2];

  snes->reason = SNES_CONVERGED_ITERATING;

  ierr       = PetscObjectSAWsTakeAccess((PetscObject)snes);CHKERRQ(ierr);
  snes->iter = 0;
  snes->norm = 0.;
  ierr       = PetscObjectSAWsGrantAccess((PetscObject)snes);CHKERRQ(ierr);

  if (snes->npc && snes->npcside== PC_LEFT && snes->functype == SNES_FUNCTION_PRECONDITIONED) {
    ierr = SNESApplyNPC(snes,X,NULL,F);CHKERRQ(ierr);
    ierr = SNESGetConvergedReason(snes->npc,&reason);CHKERRQ(ierr);
    if (reason < 0  && reason != SNES_DIVERGED_MAX_IT) {
      snes->reason = SNES_DIVERGED_INNER;
      PetscFunctionReturn(0);
    }
    ierr = VecNorm(F,NORM_2,&fnorm);CHKERRQ(ierr);
  } else {
    if (!snes->vec_func_init_set) {
      ierr = SNESComputeFunction(snes,X,F);CHKERRQ(ierr);
    } else snes->vec_func_init_set = PETSC_FALSE;

    ierr = VecNorm(F,NORM_2,&fnorm);CHKERRQ(ierr);
    SNESCheckFunctionNorm(snes,fnorm);
  }
  if (snes->npc && snes->npcside== PC_LEFT && snes->functype == SNES_FUNCTION_UNPRECONDITIONED) {
    ierr = SNESApplyNPC(snes,X,F,D);CHKERRQ(ierr);
    ierr = SNESGetConvergedReason(snes->npc,&reason);CHKERRQ(ierr);
    if (reason < 0  && reason != SNES_DIVERGED_MAX_IT) {
      snes->reason = SNES_DIVERGED_INNER;
      PetscFunctionReturn(0);
    }
  } else {
    ierr = VecCopy(F,D);CHKERRQ(ierr);
  }

  ierr       = PetscObjectSAWsTakeAccess((PetscObject)snes);CHKERRQ(ierr);
  snes->norm = fnorm;
  ierr       = PetscObjectSAWsGrantAccess((PetscObject)snes);CHKERRQ(ierr);
  ierr       = SNESLogConvergenceHistory(snes,fnorm,0);CHKERRQ(ierr);
  ierr       = SNESMonitor(snes,0,fnorm);CHKERRQ(ierr);

  /* test convergence */
  ierr = (*snes->ops->converged)(snes,0,0.0,0.0,fnorm,&snes->reason,snes->cnvP);CHKERRQ(ierr);
  if (snes->reason) PetscFunctionReturn(0);

  if (snes->npc && snes->npcside== PC_RIGHT) {
    ierr = PetscLogEventBegin(SNES_NPCSolve,snes->npc,X,0,0);CHKERRQ(ierr);
    ierr = SNESSolve(snes->npc,snes->vec_rhs,X);CHKERRQ(ierr);
    ierr = PetscLogEventEnd(SNES_NPCSolve,snes->npc,X,0,0);CHKERRQ(ierr);
    ierr = SNESGetConvergedReason(snes->npc,&reason);CHKERRQ(ierr);
    if (reason < 0 && reason != SNES_DIVERGED_MAX_IT) {
      snes->reason = SNES_DIVERGED_INNER;
      PetscFunctionReturn(0);
    }
    ierr = SNESGetNPCFunction(snes,F,&fnorm);CHKERRQ(ierr);
    ierr = VecCopy(F,D);CHKERRQ(ierr);
  }

  /* general purpose update */
  if (snes->ops->update) {
    ierr = (*snes->ops->update)(snes, snes->iter);CHKERRQ(ierr);
  }

  /* scale the initial update */
  if (qn->scale_type == SNES_QN_SCALE_JACOBIAN) {
    ierr = SNESComputeJacobian(snes,X,snes->jacobian,snes->jacobian_pre);CHKERRQ(ierr);
    SNESCheckJacobianDomainerror(snes);
    ierr = KSPSetOperators(snes->ksp,snes->jacobian,snes->jacobian_pre);CHKERRQ(ierr);
    ierr = MatLMVMSetJ0KSP(qn->B, snes->ksp);CHKERRQ(ierr);
  }

  for (i = 0, i_r = 0; i < snes->max_its; i++, i_r++) {
    /* update QN approx and calculate step */
    ierr = MatLMVMUpdate(qn->B, X, D);CHKERRQ(ierr);
    ierr = MatSolve(qn->B, D, Y);CHKERRQ(ierr);

    /* line search for lambda */
    ynorm = 1; gnorm = fnorm;
    ierr  = VecCopy(D, Dold);CHKERRQ(ierr);
    ierr  = VecCopy(X, Xold);CHKERRQ(ierr);
    ierr  = SNESLineSearchApply(snes->linesearch, X, F, &fnorm, Y);CHKERRQ(ierr);
    if (snes->reason == SNES_DIVERGED_FUNCTION_COUNT) break;
    ierr = SNESLineSearchGetReason(snes->linesearch, &lssucceed);CHKERRQ(ierr);
    ierr = SNESLineSearchGetNorms(snes->linesearch, &xnorm, &fnorm, &ynorm);CHKERRQ(ierr);
    badstep = PETSC_FALSE;
    if (lssucceed) {
      if (++snes->numFailures >= snes->maxFailures) {
        snes->reason = SNES_DIVERGED_LINE_SEARCH;
        break;
      }
      badstep = PETSC_TRUE;
    }

    /* convergence monitoring */
    ierr = PetscInfo(snes,"fnorm=%18.16e, gnorm=%18.16e, ynorm=%18.16e, lssucceed=%d\n",(double)fnorm,(double)gnorm,(double)ynorm,(int)lssucceed);CHKERRQ(ierr);

    if (snes->npc && snes->npcside== PC_RIGHT) {
      ierr = PetscLogEventBegin(SNES_NPCSolve,snes->npc,X,0,0);CHKERRQ(ierr);
      ierr = SNESSolve(snes->npc,snes->vec_rhs,X);CHKERRQ(ierr);
      ierr = PetscLogEventEnd(SNES_NPCSolve,snes->npc,X,0,0);CHKERRQ(ierr);
      ierr = SNESGetConvergedReason(snes->npc,&reason);CHKERRQ(ierr);
      if (reason < 0 && reason != SNES_DIVERGED_MAX_IT) {
        snes->reason = SNES_DIVERGED_INNER;
        PetscFunctionReturn(0);
      }
      ierr = SNESGetNPCFunction(snes,F,&fnorm);CHKERRQ(ierr);
    }

    ierr = SNESSetIterationNumber(snes, i+1);CHKERRQ(ierr);
    snes->norm = fnorm;
    snes->xnorm = xnorm;
    snes->ynorm = ynorm;

    ierr = SNESLogConvergenceHistory(snes,snes->norm,snes->iter);CHKERRQ(ierr);
    ierr = SNESMonitor(snes,snes->iter,snes->norm);CHKERRQ(ierr);

    /* set parameter for default relative tolerance convergence test */
    ierr = (*snes->ops->converged)(snes,snes->iter,xnorm,ynorm,fnorm,&snes->reason,snes->cnvP);CHKERRQ(ierr);
    if (snes->reason) PetscFunctionReturn(0);
    if (snes->npc && snes->npcside== PC_LEFT && snes->functype == SNES_FUNCTION_UNPRECONDITIONED) {
      ierr = SNESApplyNPC(snes,X,F,D);CHKERRQ(ierr);
      ierr = SNESGetConvergedReason(snes->npc,&reason);CHKERRQ(ierr);
      if (reason < 0  && reason != SNES_DIVERGED_MAX_IT) {
        snes->reason = SNES_DIVERGED_INNER;
        PetscFunctionReturn(0);
      }
    } else {
      ierr = VecCopy(F, D);CHKERRQ(ierr);
    }

    /* general purpose update */
    if (snes->ops->update) {
      ierr = (*snes->ops->update)(snes, snes->iter);CHKERRQ(ierr);
    }

    /* restart conditions */
    powell = PETSC_FALSE;
    if (qn->restart_type == SNES_QN_RESTART_POWELL && i_r > 1) {
      /* check restart by Powell's Criterion: |F^T H_0 Fold| > powell_gamma * |Fold^T H_0 Fold| */
      if (qn->scale_type == SNES_QN_SCALE_JACOBIAN) {
        ierr = MatMult(snes->jacobian_pre,Dold,W);CHKERRQ(ierr);
      } else {
        ierr = VecCopy(Dold,W);CHKERRQ(ierr);
      }
      ierr = VecDotBegin(W, Dold, &DolddotDold);CHKERRQ(ierr);
      ierr = VecDotBegin(W, D, &DolddotD);CHKERRQ(ierr);
      ierr = VecDotEnd(W, Dold, &DolddotDold);CHKERRQ(ierr);
      ierr = VecDotEnd(W, D, &DolddotD);CHKERRQ(ierr);
      if (PetscAbs(PetscRealPart(DolddotD)) > qn->powell_gamma*PetscAbs(PetscRealPart(DolddotDold))) powell = PETSC_TRUE;
    }
    periodic = PETSC_FALSE;
    if (qn->restart_type == SNES_QN_RESTART_PERIODIC) {
      if (i_r>qn->m-1) periodic = PETSC_TRUE;
    }
    /* restart if either powell or periodic restart is satisfied. */
    if (badstep || powell || periodic) {
      if (qn->monflg) {
        ierr = PetscViewerASCIIAddTab(qn->monitor,((PetscObject)snes)->tablevel+2);CHKERRQ(ierr);
        if (powell) {
          ierr = PetscViewerASCIIPrintf(qn->monitor, "Powell restart! |%14.12e| > %6.4f*|%14.12e| i_r = %D\n", (double)PetscRealPart(DolddotD), (double)qn->powell_gamma, (double)PetscRealPart(DolddotDold),i_r);CHKERRQ(ierr);
        } else {
          ierr = PetscViewerASCIIPrintf(qn->monitor, "Periodic restart! i_r = %D\n", i_r);CHKERRQ(ierr);
        }
        ierr = PetscViewerASCIISubtractTab(qn->monitor,((PetscObject)snes)->tablevel+2);CHKERRQ(ierr);
      }
      i_r = -1;
      if (qn->scale_type == SNES_QN_SCALE_JACOBIAN) {
        ierr = SNESComputeJacobian(snes,X,snes->jacobian,snes->jacobian_pre);CHKERRQ(ierr);
        SNESCheckJacobianDomainerror(snes);
      }
      ierr = MatLMVMReset(qn->B, PETSC_FALSE);CHKERRQ(ierr);
    }
  }
  if (i == snes->max_its) {
    ierr = PetscInfo(snes, "Maximum number of iterations has been reached: %D\n", snes->max_its);CHKERRQ(ierr);
    if (!snes->reason) snes->reason = SNES_DIVERGED_MAX_IT;
  }
  PetscFunctionReturn(0);
}

static PetscErrorCode SNESSetUp_QN(SNES snes)
{
  SNES_QN        *qn = (SNES_QN*)snes->data;
  PetscErrorCode ierr;
  DM             dm;
  PetscInt       n, N;

  PetscFunctionBegin;

  if (!snes->vec_sol) {
    ierr = SNESGetDM(snes,&dm);CHKERRQ(ierr);
    ierr = DMCreateGlobalVector(dm,&snes->vec_sol);CHKERRQ(ierr);
  }
  ierr = SNESSetWorkVecs(snes,4);CHKERRQ(ierr);

  if (qn->scale_type == SNES_QN_SCALE_JACOBIAN) {
    ierr = SNESSetUpMatrices(snes);CHKERRQ(ierr);
  }
  if (snes->npcside== PC_LEFT && snes->functype == SNES_FUNCTION_DEFAULT) {snes->functype = SNES_FUNCTION_UNPRECONDITIONED;}

  /* set method defaults */
  if (qn->scale_type == SNES_QN_SCALE_DEFAULT) {
    if (qn->type == SNES_QN_BADBROYDEN) {
      qn->scale_type = SNES_QN_SCALE_NONE;
    } else {
      qn->scale_type = SNES_QN_SCALE_SCALAR;
    }
  }
  if (qn->restart_type == SNES_QN_RESTART_DEFAULT) {
    if (qn->type == SNES_QN_LBFGS) {
      qn->restart_type = SNES_QN_RESTART_POWELL;
    } else {
      qn->restart_type = SNES_QN_RESTART_PERIODIC;
    }
  }

  /* Set up the LMVM matrix */
  switch (qn->type) {
    case SNES_QN_BROYDEN:
      ierr = MatSetType(qn->B, MATLMVMBROYDEN);CHKERRQ(ierr);
      qn->scale_type = SNES_QN_SCALE_NONE;
      break;
    case SNES_QN_BADBROYDEN:
      ierr = MatSetType(qn->B, MATLMVMBADBROYDEN);CHKERRQ(ierr);
      qn->scale_type = SNES_QN_SCALE_NONE;
      break;
    default:
      ierr = MatSetType(qn->B, MATLMVMBFGS);CHKERRQ(ierr);
      switch (qn->scale_type) {
        case SNES_QN_SCALE_NONE:
          ierr = MatLMVMSymBroydenSetScaleType(qn->B, MAT_LMVM_SYMBROYDEN_SCALE_NONE);CHKERRQ(ierr);
          break;
        case SNES_QN_SCALE_SCALAR:
          ierr = MatLMVMSymBroydenSetScaleType(qn->B, MAT_LMVM_SYMBROYDEN_SCALE_SCALAR);CHKERRQ(ierr);
          break;
        case SNES_QN_SCALE_JACOBIAN:
          ierr = MatLMVMSymBroydenSetScaleType(qn->B, MAT_LMVM_SYMBROYDEN_SCALE_USER);CHKERRQ(ierr);
          break;
        case SNES_QN_SCALE_DIAGONAL:
        case SNES_QN_SCALE_DEFAULT:
        default:
          break;
      }
      break;
  }
  ierr = VecGetLocalSize(snes->vec_sol, &n);CHKERRQ(ierr);
  ierr = VecGetSize(snes->vec_sol, &N);CHKERRQ(ierr);
  ierr = MatSetSizes(qn->B, n, n, N, N);CHKERRQ(ierr);
  ierr = MatSetUp(qn->B);CHKERRQ(ierr);
  ierr = MatLMVMReset(qn->B, PETSC_TRUE);CHKERRQ(ierr);
  ierr = MatLMVMSetHistorySize(qn->B, qn->m);CHKERRQ(ierr);
  ierr = MatLMVMAllocate(qn->B, snes->vec_sol, snes->vec_func);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode SNESReset_QN(SNES snes)
{
  PetscErrorCode ierr;
  SNES_QN        *qn;

  PetscFunctionBegin;
  if (snes->data) {
    qn = (SNES_QN*)snes->data;
    ierr = MatDestroy(&qn->B);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

static PetscErrorCode SNESDestroy_QN(SNES snes)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = SNESReset_QN(snes);CHKERRQ(ierr);
  ierr = PetscFree(snes->data);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)snes,"",NULL);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode SNESSetFromOptions_QN(PetscOptionItems *PetscOptionsObject,SNES snes)
{

  PetscErrorCode    ierr;
  SNES_QN           *qn    = (SNES_QN*)snes->data;
  PetscBool         flg;
  SNESLineSearch    linesearch;
  SNESQNRestartType rtype = qn->restart_type;
  SNESQNScaleType   stype = qn->scale_type;
  SNESQNType        qtype = qn->type;

  PetscFunctionBegin;
  ierr = PetscOptionsHead(PetscOptionsObject,"SNES QN options");CHKERRQ(ierr);
  ierr = PetscOptionsInt("-snes_qn_m","Number of past states saved for L-BFGS methods","SNESQN",qn->m,&qn->m,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsReal("-snes_qn_powell_gamma","Powell angle tolerance",          "SNESQN", qn->powell_gamma, &qn->powell_gamma, NULL);CHKERRQ(ierr);
  ierr = PetscOptionsBool("-snes_qn_monitor",         "Monitor for the QN methods",      "SNESQN", qn->monflg, &qn->monflg, NULL);CHKERRQ(ierr);
  ierr = PetscOptionsEnum("-snes_qn_scale_type","Scaling type","SNESQNSetScaleType",SNESQNScaleTypes,(PetscEnum)stype,(PetscEnum*)&stype,&flg);CHKERRQ(ierr);
  if (flg) {ierr = SNESQNSetScaleType(snes,stype);CHKERRQ(ierr);}

  ierr = PetscOptionsEnum("-snes_qn_restart_type","Restart type","SNESQNSetRestartType",SNESQNRestartTypes,(PetscEnum)rtype,(PetscEnum*)&rtype,&flg);CHKERRQ(ierr);
  if (flg) {ierr = SNESQNSetRestartType(snes,rtype);CHKERRQ(ierr);}

  ierr = PetscOptionsEnum("-snes_qn_type","Quasi-Newton update type","",SNESQNTypes,(PetscEnum)qtype,(PetscEnum*)&qtype,&flg);CHKERRQ(ierr);
  if (flg) {ierr = SNESQNSetType(snes,qtype);CHKERRQ(ierr);}
  ierr = MatSetFromOptions(qn->B);CHKERRQ(ierr);
  ierr = PetscOptionsTail();CHKERRQ(ierr);
  if (!snes->linesearch) {
    ierr = SNESGetLineSearch(snes, &linesearch);CHKERRQ(ierr);
    if (!((PetscObject)linesearch)->type_name) {
      if (qn->type == SNES_QN_LBFGS) {
        ierr = SNESLineSearchSetType(linesearch, SNESLINESEARCHCP);CHKERRQ(ierr);
      } else if (qn->type == SNES_QN_BROYDEN) {
        ierr = SNESLineSearchSetType(linesearch, SNESLINESEARCHBASIC);CHKERRQ(ierr);
      } else {
        ierr = SNESLineSearchSetType(linesearch, SNESLINESEARCHL2);CHKERRQ(ierr);
      }
    }
  }
  if (qn->monflg) {
    ierr = PetscViewerASCIIGetStdout(PetscObjectComm((PetscObject)snes), &qn->monitor);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

static PetscErrorCode SNESView_QN(SNES snes, PetscViewer viewer)
{
  SNES_QN        *qn    = (SNES_QN*)snes->data;
  PetscBool      iascii;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = PetscObjectTypeCompare((PetscObject) viewer, PETSCVIEWERASCII, &iascii);CHKERRQ(ierr);
  if (iascii) {
    ierr = PetscViewerASCIIPrintf(viewer,"  type is %s, restart type is %s, scale type is %s\n",SNESQNTypes[qn->type],SNESQNRestartTypes[qn->restart_type],SNESQNScaleTypes[qn->scale_type]);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"  Stored subspace size: %D\n", qn->m);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

/*@
    SNESQNSetRestartType - Sets the restart type for SNESQN.

    Logically Collective on SNES

    Input Parameters:
+   snes - the iterative context
-   rtype - restart type

    Options Database:
+   -snes_qn_restart_type <powell,periodic,none> - set the restart type
-   -snes_qn_m <m> - sets the number of stored updates and the restart period for periodic

    Level: intermediate

    SNESQNRestartTypes:
+   SNES_QN_RESTART_NONE - never restart
.   SNES_QN_RESTART_POWELL - restart based upon descent criteria
-   SNES_QN_RESTART_PERIODIC - restart after a fixed number of iterations

@*/
PetscErrorCode SNESQNSetRestartType(SNES snes, SNESQNRestartType rtype)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_CLASSID,1);
  ierr = PetscTryMethod(snes,"SNESQNSetRestartType_C",(SNES,SNESQNRestartType),(snes,rtype));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/*@
    SNESQNSetScaleType - Sets the scaling type for the inner inverse Jacobian in SNESQN.

    Logically Collective on SNES

    Input Parameters:
+   snes - the iterative context
-   stype - scale type

    Options Database:
.   -snes_qn_scale_type <diagonal,none,scalar,jacobian>

    Level: intermediate

    SNESQNScaleTypes:
+   SNES_QN_SCALE_NONE - don't scale the problem
.   SNES_QN_SCALE_SCALAR - use Shanno scaling
.   SNES_QN_SCALE_DIAGONAL - scale with a diagonalized BFGS formula (see Gilbert and Lemarechal 1989), available
-   SNES_QN_SCALE_JACOBIAN - scale by solving a linear system coming from the Jacobian you provided with SNESSetJacobian() computed at the first iteration
                             of QN and at ever restart.

.seealso: SNES, SNESQN, SNESLineSearch, SNESQNScaleType, SNESSetJacobian()
@*/

PetscErrorCode SNESQNSetScaleType(SNES snes, SNESQNScaleType stype)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_CLASSID,1);
  ierr = PetscTryMethod(snes,"SNESQNSetScaleType_C",(SNES,SNESQNScaleType),(snes,stype));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PetscErrorCode SNESQNSetScaleType_QN(SNES snes, SNESQNScaleType stype)
{
  SNES_QN *qn = (SNES_QN*)snes->data;

  PetscFunctionBegin;
  qn->scale_type = stype;
  if (stype == SNES_QN_SCALE_JACOBIAN) snes->usesksp = PETSC_TRUE;
  PetscFunctionReturn(0);
}

PetscErrorCode SNESQNSetRestartType_QN(SNES snes, SNESQNRestartType rtype)
{
  SNES_QN *qn = (SNES_QN*)snes->data;

  PetscFunctionBegin;
  qn->restart_type = rtype;
  PetscFunctionReturn(0);
}

/*@
    SNESQNSetType - Sets the quasi-Newton variant to be used in SNESQN.

    Logically Collective on SNES

    Input Parameters:
+   snes - the iterative context
-   qtype - variant type

    Options Database:
.   -snes_qn_type <lbfgs,broyden,badbroyden>

    Level: beginner

    SNESQNTypes:
+   SNES_QN_LBFGS - LBFGS variant
.   SNES_QN_BROYDEN - Broyden variant
-   SNES_QN_BADBROYDEN - Bad Broyden variant

@*/

PetscErrorCode SNESQNSetType(SNES snes, SNESQNType qtype)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_CLASSID,1);
  ierr = PetscTryMethod(snes,"SNESQNSetType_C",(SNES,SNESQNType),(snes,qtype));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PetscErrorCode SNESQNSetType_QN(SNES snes, SNESQNType qtype)
{
  SNES_QN *qn = (SNES_QN*)snes->data;

  PetscFunctionBegin;
  qn->type = qtype;
  PetscFunctionReturn(0);
}

/* -------------------------------------------------------------------------- */
/*MC
      SNESQN - Limited-Memory Quasi-Newton methods for the solution of nonlinear systems.

      Options Database:

+     -snes_qn_m <m> - Number of past states saved for the L-Broyden methods.
+     -snes_qn_restart_type <powell,periodic,none> - set the restart type
.     -snes_qn_powell_gamma - Angle condition for restart.
.     -snes_qn_powell_descent - Descent condition for restart.
.     -snes_qn_type <lbfgs,broyden,badbroyden> - QN type
.     -snes_qn_scale_type <diagonal,none,scalar,jacobian> - scaling performed on inner Jacobian
.     -snes_linesearch_type <cp, l2, basic> - Type of line search.
-     -snes_qn_monitor - Monitors the quasi-newton Jacobian.

      Notes:
    This implements the L-BFGS, Broyden, and "Bad" Broyden algorithms for the solution of F(x) = b using
      previous change in F(x) and x to form the approximate inverse Jacobian using a series of multiplicative rank-one
      updates.

      When using a nonlinear preconditioner, one has two options as to how the preconditioner is applied.  The first of
      these options, sequential, uses the preconditioner to generate a new solution and function and uses those at this
      iteration as the current iteration's values when constructing the approximate Jacobian.  The second, composed,
      perturbs the problem the Jacobian represents to be P(x, b) - x = 0, where P(x, b) is the preconditioner.

      Uses left nonlinear preconditioning by default.

      References:
+   1. -   Kelley, C.T., Iterative Methods for Linear and Nonlinear Equations, Chapter 8, SIAM, 1995.
.   2. -   R. Byrd, J. Nocedal, R. Schnabel, Representations of Quasi Newton Matrices and their use in Limited Memory Methods,
      Technical Report, Northwestern University, June 1992.
.   3. -   Peter N. Brown, Alan C. Hindmarsh, Homer F. Walker, Experiments with Quasi-Newton Methods in Solving Stiff ODE
      Systems, SIAM J. Sci. Stat. Comput. Vol 6(2), April 1985.
.   4. -   Peter R. Brune, Matthew G. Knepley, Barry F. Smith, and Xuemin Tu, "Composing Scalable Nonlinear Algebraic Solvers",
       SIAM Review, 57(4), 2015
.   5. -   Griewank, Andreas. "Broyden updating, the good and the bad!." Doc. Math (2012): 301-315.
.   6. -   Gilbert, Jean Charles, and Claude Lemar{\'e}chal. "Some numerical experiments with variable-storage quasi-Newton algorithms."
      Mathematical programming 45.1-3 (1989): 407-435.
-   7. -   Dener A., Munson T. "Accelerating Limited-Memory Quasi-Newton Convergence for Large-Scale Optimization"
      Computational Science - ICCS 2019. ICCS 2019. Lecture Notes in Computer Science, vol 11538. Springer, Cham

      Level: beginner

.seealso:  SNESCreate(), SNES, SNESSetType(), SNESNEWTONLS, SNESNEWTONTR

M*/
PETSC_EXTERN PetscErrorCode SNESCreate_QN(SNES snes)
{
  PetscErrorCode ierr;
  SNES_QN        *qn;
  const char     *optionsprefix;

  PetscFunctionBegin;
  snes->ops->setup          = SNESSetUp_QN;
  snes->ops->solve          = SNESSolve_QN;
  snes->ops->destroy        = SNESDestroy_QN;
  snes->ops->setfromoptions = SNESSetFromOptions_QN;
  snes->ops->view           = SNESView_QN;
  snes->ops->reset          = SNESReset_QN;

  snes->npcside= PC_LEFT;

  snes->usesnpc = PETSC_TRUE;
  snes->usesksp = PETSC_FALSE;

  snes->alwayscomputesfinalresidual = PETSC_TRUE;

  if (!snes->tolerancesset) {
    snes->max_funcs = 30000;
    snes->max_its   = 10000;
  }

  ierr                = PetscNewLog(snes,&qn);CHKERRQ(ierr);
  snes->data          = (void*) qn;
  qn->m               = 10;
  qn->scaling         = 1.0;
  qn->monitor         = NULL;
  qn->monflg          = PETSC_FALSE;
  qn->powell_gamma    = 0.9999;
  qn->scale_type      = SNES_QN_SCALE_DEFAULT;
  qn->restart_type    = SNES_QN_RESTART_DEFAULT;
  qn->type            = SNES_QN_LBFGS;

  ierr = MatCreate(PetscObjectComm((PetscObject)snes), &qn->B);CHKERRQ(ierr);
  ierr = SNESGetOptionsPrefix(snes, &optionsprefix);CHKERRQ(ierr);
  ierr = MatSetOptionsPrefix(qn->B, optionsprefix);CHKERRQ(ierr);

  ierr = PetscObjectComposeFunction((PetscObject)snes,"SNESQNSetScaleType_C",SNESQNSetScaleType_QN);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)snes,"SNESQNSetRestartType_C",SNESQNSetRestartType_QN);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)snes,"SNESQNSetType_C",SNESQNSetType_QN);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}
