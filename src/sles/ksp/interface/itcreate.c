#ifndef lint
static char vcid[] = "$Id: itcreate.c,v 1.3 1994/08/19 02:06:38 bsmith Exp $";
#endif

#include "petsc.h"
#include "kspimpl.h"
#include <stdio.h>
#include "system/nreg.h"

static NRList *__ITList = 0;
/*@
    KSPCreate - Creates default KSP
@*/
int KSPCreate(ksp)
KSP *ksp;
{
  KSP ctx;
  *ksp = 0;
  CREATEHEADER(ctx,_KSP);
  *ksp               = ctx;
  ctx->cookie        = KSP_COOKIE;
  ctx->namemethod    = "-kspmethod";
  ctx->namemax_it    = "-kspmax_it";
  ctx->nameright_pre = "-kspright_pre";
  ctx->nameuse_pres  = "-kspuse_pres";
  ctx->namertol      = "-ksprtol";
  ctx->nameatol      = "-kspatol";
  ctx->namedivtol    = "-kspdivtol";

  ctx->method        = -1;
  ctx->max_it        = 10000;
  ctx->right_pre     = 0;
  ctx->use_pres      = 0;
  ctx->rtol          = 1.e-5;
  ctx->atol          = 1.e-50;
  ctx->divtol        = 1.e4;

  ctx->guess_zero    = 0;
  ctx->calc_eigs     = 0;
  ctx->calc_res      = 0;
  ctx->residual_history = 0;
  ctx->res_hist_size    = 0;
  ctx->res_act_size     = 0;
  ctx->converged = KSPDefaultConverged;
  ctx->usr_monitor= 0;
  ctx->adjust_work_vectors = 0;
  ctx->BuildSolution = KSPDefaultBuildSolution;
  ctx->BuildResidual = KSPDefaultBuildResidual;

  ctx->vec_sol   = 0;
  ctx->vec_rhs   = 0;
  ctx->amult     = 0;
  ctx->binv      = 0;
  ctx->matop     = 0;
  ctx->tamult    = 0;
  ctx->tbinv     = 0;
  ctx->tmatop    = 0;

  ctx->solver    = 0;
  ctx->setup     = 0;
  ctx->destroy   = 0;
  ctx->adjustwork= 0;

  ctx->MethodPrivate = 0;
  ctx->nwork         = 0;
  ctx->work          = 0;

  ctx->nmatop        = 0;
  ctx->namult        = 0;
  ctx->nbinv         = 0;
  ctx->nvectors      = 0;
  ctx->nscalar       = 0;

  ctx->amultP        = 0;
  ctx->binvP         = 0;
  ctx->monP          = 0;
  ctx->cnvP          = 0;

  ctx->setupcalled   = 0;

  return 0;
}

/*@
  KSPSetMethod - Builds KSP for a particular solver. Itmethod is,
  for instance, KSPCG or KSPGMRES.  

  Input Parameter:
.  itmethod   - One of the known methods.  See "ksp.h" for
    available methods (for instance KSPCG or KSPGMRES).
 @*/
int KSPSetMethod(ctx,itmethod)
KSP    ctx;
KSPMETHOD itmethod;
{
  int (*r)();
  VALIDHEADER(ctx,KSP_COOKIE);
  if (ctx->setupcalled) {
    SETERR(1,"Method cannot be called after KSPSetUp");
  }
  /* Get the function pointers for the iterative method requested */
  if (!__ITList) {KSPRegisterAll();}
  if (!__ITList) {
    SETERR(1,"Could not acquire list of KSP methods"); 
  }
  r =  (int (*)())NRFindRoutine( __ITList, (int)itmethod, (char *)0 );
  if (!r) {SETERR(1,"Unknown KSP method");}
  if (ctx->MethodPrivate) FREE(ctx->MethodPrivate);
  return (*r)(ctx);
}

/*@C
   KSPRegister - Adds the iterative method to the KSP package,  given
   an iterative name (KSPMETHOD) and a function pointer.

   Input Parameters:
.      name - for instance KSPGMRES, ...
.      sname -  corresponding string for name
.      create - routine to create method context
@*/
int  KSPRegister(name, sname, create)
KSPMETHOD  name;
char       *sname;
void       (*create)();
{
  if (!__ITList) __ITList = NRCreate();
  return NRRegister( __ITList, (int) name, sname, (void (*)())create );
}

/*@
   KSPRegisterDestroy - Frees the list of iterative solvers
   registered by KSPRegister().
@*/
int KSPRegisterDestroy()
{
  if (__ITList) {
    NRDestroy( __ITList );
    __ITList = 0;
  }
  return 0;
}

/*@C
  KSPGetMethodFromCommandLine - Sets the selected method,
                         given the argument list.

  Input parameters:
. Argc - pointer to arg count
. argv - argument vector
. flag - 1 if argument should be removed from list if found 
. sname - name used to indicate solver.  If null, -itmethod is used

  Output parameter:
. kspmethod -  Iterative method type
. returns 1 if method found else 0.
@*/
int KSPGetMethodFromCommandLine( Argc, argv, flag, sname, itmethod )
int      *Argc, flag;
char     **argv, *sname;
KSPMETHOD *itmethod;
{
  char sbuf[50];
  if (!sname) sname = "-kspmethod";
  if (SYArgGetString( Argc, argv, flag, sname, sbuf, 50 )) {
    if (!__ITList) KSPRegisterAll();
    *itmethod = (KSPMETHOD)NRFindID( __ITList, sbuf );
    return 1;
  }
  return 0;
}

/*@C
   KSPGetMethodName - Get the name (as a string) from the method type

   Input Parameter:
.  itctx - Iterative context
@*/
int KSPGetMethodName( itmeth,name )
KSPMETHOD itmeth;
char      **name;
{
  if (!__ITList) KSPRegisterAll();
  *name = NRFindName( __ITList, itmeth );
  return 0;
}

#include <stdio.h>

int KSPPrintMethods(name)
{
  FuncList *entry;
  if (!__ITList) {KSPRegisterAll();}
  entry = __ITList->head;
  fprintf(stderr," %s (one of)",name);
  while (entry) {
    fprintf(stderr," %s",entry->name);
    entry = entry->next;
  }
  fprintf(stderr,"\n");
  return 1;
}
