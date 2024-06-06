static char help[] = "Applies the 2023 preconditioner of Benzi and Faccio\n\n";

#include <petscmat.h>
#include <petscviewer.h>
#include <petscvec.h>
#include <petscis.h>
#include <petscksp.h>

/*
 * This example reproduces the preconditioner outlined in Benzi's paper
 * https://doi.org/10.1137/22M1505529. The problem considered is:
 *
 * (A + gamma UU^T)x = b
 *
 * whose structure arises from, for example, grad-div stabilization in the
 * Navier-Stokes momentum equation. In the code we will also refer to
 * gamma UU^T as J. The preconditioner developed by Benzi is:
 *
 * P_alpha = (A + alpha I)(alpha I + gamma UU^T)
 *
 * Another variant which may yield better convergence depending on the specific
 * problem is
 *
 * P_alpha = (A + alpha D) D^-1 (alpha D + gamma UU^T)
 *
 * where D = diag(A + gamma UU^T). This is the variant implemented
 * here. Application of the preconditioner involves (approximate) solution of
 * two systems, one with (A + alpha D), and another with (alpha D + gamma
 * UU^T). For small alpha (which generally yields the best overall
 * preconditioner), (alpha D + gamma UU^T) is ill-conditioned. To combat this we
 * solve (alpha D + gamma UU^T) using the Sherman-Morrison-Woodbury (SMW) matrix
 * identity, which effectively converts the grad-div structure to a much nicer
 * div-grad (laplacian) structure.
 *
 * The matrices used as input can be generated by running the matlab/octave
 * program IFISS. The particular matrices checked into the datafiles repository
 * and used in testing of this example correspond to a leaky lid-driven cavity
 * with a stretched grid and Q2-Q1 finite elements. The matrices are taken from
 * the last iteration of a Picard solve with tolerance 1e-8 with a viscosity of
 * 0.1 and a 32x32 grid. We summarize below iteration counts from running this
 * preconditioner for different grids and viscosity with a KSP tolerance of 1e-6.
 *
 *       32x32 64x64 128x128
 * 0.1   28    36    43
 * 0.01  59    75    73
 * 0.002 136   161   167
 *
 * A reader of Benzi's paper will note that the performance shown above with
 * respect to decreasing viscosity is significantly worse than in the
 * paper. This is actually because of the choice of RHS. In Benzi's work, the
 * RHS was generated by multiplying the operator with a vector of 1s whereas
 * here we generate the RHS using a random vector. The iteration counts from the
 * Benzi paper can be reproduced by changing the RHS generation in this example,
 * but we choose to use the more difficult RHS as the resulting performance may
 * more closely match what users experience in "physical" contexts.
 */

PetscErrorCode CreateAndLoadMat(const char *mat_name, Mat *mat)
{
  PetscViewer viewer;
  char        file[PETSC_MAX_PATH_LEN];
  char        flag_name[10] = "-f";
  PetscBool   flg;

  PetscFunctionBeginUser;
  PetscCall(PetscOptionsGetString(NULL, NULL, strcat(flag_name, mat_name), file, sizeof(file), &flg));
  PetscCheck(flg, PETSC_COMM_WORLD, PETSC_ERR_USER, "Must indicate file with the -f<mat_name> option");
  PetscCall(PetscViewerBinaryOpen(PETSC_COMM_WORLD, file, FILE_MODE_READ, &viewer));
  PetscCall(MatCreate(PETSC_COMM_WORLD, mat));
  PetscCall(MatSetType(*mat, MATAIJ));
  PetscCall(PetscObjectSetName((PetscObject)*mat, mat_name));
  PetscCall(MatSetFromOptions(*mat));
  PetscCall(MatLoad(*mat, viewer));
  PetscCall(PetscViewerDestroy(&viewer));
  PetscFunctionReturn(PETSC_SUCCESS);
}

typedef struct {
  Mat       U, UT, D, aD, aDinv, I_plus_gammaUTaDinvU;
  PC        smw_cholesky;
  PetscReal gamma, alpha;
  PetscBool setup_called;
} SmwPCCtx;

PetscErrorCode SmwSetup(PC pc)
{
  SmwPCCtx *ctx;
  Vec       aDVec;

  PetscFunctionBeginUser;
  PetscCall(PCShellGetContext(pc, &ctx));

  if (ctx->setup_called) PetscFunctionReturn(PETSC_SUCCESS);

  // Create aD
  PetscCall(MatDuplicate(ctx->D, MAT_COPY_VALUES, &ctx->aD));
  PetscCall(MatScale(ctx->aD, ctx->alpha));

  // Create aDinv
  PetscCall(MatDuplicate(ctx->aD, MAT_DO_NOT_COPY_VALUES, &ctx->aDinv));
  PetscCall(MatCreateVecs(ctx->aD, &aDVec, NULL));
  PetscCall(MatGetDiagonal(ctx->aD, aDVec));
  PetscCall(VecReciprocal(aDVec));
  PetscCall(MatDiagonalSet(ctx->aDinv, aDVec, INSERT_VALUES));

  // Create UT
  PetscCall(MatTranspose(ctx->U, MAT_INITIAL_MATRIX, &ctx->UT));

  // Create sum Mat
  PetscCall(MatMatMatMult(ctx->UT, ctx->aDinv, ctx->U, MAT_INITIAL_MATRIX, PETSC_DEFAULT, &ctx->I_plus_gammaUTaDinvU));
  PetscCall(MatScale(ctx->I_plus_gammaUTaDinvU, ctx->gamma));
  PetscCall(MatShift(ctx->I_plus_gammaUTaDinvU, 1.));

  PetscCall(PCCreate(PETSC_COMM_WORLD, &ctx->smw_cholesky));
  PetscCall(PCSetType(ctx->smw_cholesky, PCCHOLESKY));
  PetscCall(PCSetOperators(ctx->smw_cholesky, ctx->I_plus_gammaUTaDinvU, ctx->I_plus_gammaUTaDinvU));
  PetscCall(PCSetOptionsPrefix(ctx->smw_cholesky, "smw_"));
  PetscCall(PCSetFromOptions(ctx->smw_cholesky));
  PetscCall(PCSetUp(ctx->smw_cholesky));

  PetscCall(VecDestroy(&aDVec));

  ctx->setup_called = PETSC_TRUE;
  PetscFunctionReturn(PETSC_SUCCESS);
}

PetscErrorCode SmwApply(PC pc, Vec x, Vec y)
{
  SmwPCCtx *ctx;
  Vec       vel0, pressure0, pressure1;

  PetscFunctionBeginUser;
  PetscCall(PCShellGetContext(pc, &ctx));

  PetscCall(MatCreateVecs(ctx->UT, &vel0, &pressure0));
  PetscCall(VecDuplicate(pressure0, &pressure1));

  // First term
  PetscCall(MatMult(ctx->aDinv, x, vel0));
  PetscCall(MatMult(ctx->UT, vel0, pressure0));
  PetscCall(PCApply(ctx->smw_cholesky, pressure0, pressure1));
  PetscCall(MatMult(ctx->U, pressure1, vel0));
  PetscCall(MatMult(ctx->aDinv, vel0, y));
  PetscCall(VecScale(y, -ctx->gamma));

  // Second term
  PetscCall(MatMult(ctx->aDinv, x, vel0));

  PetscCall(VecAXPY(y, 1, vel0));

  PetscCall(VecDestroy(&vel0));
  PetscCall(VecDestroy(&pressure0));
  PetscCall(VecDestroy(&pressure1));
  PetscFunctionReturn(PETSC_SUCCESS);
}

int main(int argc, char **args)
{
  Mat               A, B, Q, Acondensed, Bcondensed, BT, J, AplusJ, QInv, D, AplusD, JplusD, U;
  Mat               AplusJarray[2];
  Vec               bound, x, b, Qdiag, DVec;
  PetscBool         flg;
  PetscViewer       viewer;
  char              file[PETSC_MAX_PATH_LEN];
  PetscInt         *boundary_indices;
  PetscInt          boundary_indices_size, am, an, bm, bn, condensed_am, astart, aend, Dstart, Dend, num_local_bnd_dofs = 0;
  const PetscScalar zero = 0;
  IS                boundary_is, bulk_is;
  KSP               ksp;
  PC                pc, pcA, pcJ;
  PetscRandom       rctx;
  PetscReal        *boundary_indices_values;
  PetscReal         gamma = 100, alpha = .01;
  PetscMPIInt       rank;
  SmwPCCtx          ctx;

  PetscFunctionBeginUser;
  PetscCall(PetscInitialize(&argc, &args, (char *)0, help));
  PetscCallMPI(MPI_Comm_rank(PETSC_COMM_WORLD, &rank));

  PetscCall(CreateAndLoadMat("A", &A));
  PetscCall(CreateAndLoadMat("B", &B));
  PetscCall(CreateAndLoadMat("Q", &Q));

  PetscCall(PetscOptionsGetString(NULL, NULL, "-fbound", file, sizeof(file), &flg));
  PetscCheck(flg, PETSC_COMM_WORLD, PETSC_ERR_USER, "Must indicate file with the -fbound option");

  if (rank == 0) {
    PetscCall(PetscViewerBinaryOpen(PETSC_COMM_SELF, file, FILE_MODE_READ, &viewer));
    PetscCall(VecCreate(PETSC_COMM_SELF, &bound));
    PetscCall(PetscObjectSetName((PetscObject)bound, "bound"));
    PetscCall(VecSetType(bound, VECSEQ));
    PetscCall(VecLoad(bound, viewer));
    PetscCall(PetscViewerDestroy(&viewer));
    PetscCall(VecGetLocalSize(bound, &boundary_indices_size));
    PetscCall(VecGetArray(bound, &boundary_indices_values));
  }
  PetscCallMPI(MPI_Bcast(&boundary_indices_size, 1, MPIU_INT, 0, PETSC_COMM_WORLD));
  if (rank != 0) PetscCall(PetscMalloc1(boundary_indices_size, &boundary_indices_values));
  PetscCallMPI(MPI_Bcast(boundary_indices_values, boundary_indices_size, MPIU_SCALAR, 0, PETSC_COMM_WORLD));

  PetscCall(MatGetSize(A, &am, NULL));
  // The total number of dofs for a given velocity component
  const PetscInt nc = am / 2;
  PetscCall(MatGetOwnershipRange(A, &astart, &aend));

  PetscCall(PetscMalloc1(2 * boundary_indices_size, &boundary_indices));

  //
  // The dof index ordering appears to be all vx dofs and then all vy dofs.
  //

  // First do vx
  for (PetscInt i = 0; i < boundary_indices_size; ++i) {
    // MATLAB uses 1-based indexing
    const PetscInt bnd_dof = (PetscInt)boundary_indices_values[i] - 1;
    if ((bnd_dof >= astart) && (bnd_dof < aend)) boundary_indices[num_local_bnd_dofs++] = bnd_dof;
  }

  // Now vy
  for (PetscInt i = 0; i < boundary_indices_size; ++i) {
    // MATLAB uses 1-based indexing
    const PetscInt bnd_dof = ((PetscInt)boundary_indices_values[i] - 1) + nc;
    if ((bnd_dof >= astart) && (bnd_dof < aend)) boundary_indices[num_local_bnd_dofs++] = bnd_dof;
  }
  if (rank == 0) PetscCall(VecRestoreArray(bound, &boundary_indices_values));
  else PetscCall(PetscFree(boundary_indices_values));

  PetscCall(ISCreateGeneral(PETSC_COMM_WORLD, num_local_bnd_dofs, boundary_indices, PETSC_USE_POINTER, &boundary_is));
  PetscCall(ISComplement(boundary_is, astart, aend, &bulk_is));

  PetscCall(MatCreateSubMatrix(A, bulk_is, bulk_is, MAT_INITIAL_MATRIX, &Acondensed));
  // Can't pass null for row index set :-(
  PetscCall(MatTranspose(B, MAT_INPLACE_MATRIX, &B));
  PetscCall(MatCreateSubMatrix(B, bulk_is, NULL, MAT_INITIAL_MATRIX, &Bcondensed));
  PetscCall(MatGetLocalSize(Acondensed, &am, &an));
  PetscCall(MatGetLocalSize(Bcondensed, &bm, &bn));

  // Create QInv
  PetscCall(MatCreateVecs(Q, &Qdiag, NULL));
  PetscCall(MatGetDiagonal(Q, Qdiag));
  PetscCall(VecReciprocal(Qdiag));
  PetscCall(MatDuplicate(Q, MAT_DO_NOT_COPY_VALUES, &QInv));
  PetscCall(MatDiagonalSet(QInv, Qdiag, INSERT_VALUES));
  PetscCall(MatAssemblyBegin(QInv, MAT_FINAL_ASSEMBLY));
  PetscCall(MatAssemblyEnd(QInv, MAT_FINAL_ASSEMBLY));

  // Create J
  PetscCall(MatTranspose(Bcondensed, MAT_INITIAL_MATRIX, &BT));
  PetscCall(MatMatMatMult(Bcondensed, QInv, BT, MAT_INITIAL_MATRIX, PETSC_DEFAULT, &J));
  PetscCall(MatScale(J, gamma));

  // Create sum of A + J
  AplusJarray[0] = Acondensed;
  AplusJarray[1] = J;
  PetscCall(MatCreateComposite(PETSC_COMM_WORLD, 2, AplusJarray, &AplusJ));

  // Create decomposition matrices
  // We've already used Qdiag, which currently represents Q^-1,  for its necessary purposes. Let's
  // convert it to represent Q^(-1/2)
  PetscCall(VecSqrtAbs(Qdiag));
  // We can similarly reuse Qinv
  PetscCall(MatDiagonalSet(QInv, Qdiag, INSERT_VALUES));
  PetscCall(MatAssemblyBegin(QInv, MAT_FINAL_ASSEMBLY));
  PetscCall(MatAssemblyEnd(QInv, MAT_FINAL_ASSEMBLY));
  // Create U
  PetscCall(MatMatMult(Bcondensed, QInv, MAT_INITIAL_MATRIX, PETSC_DEFAULT, &U));

  // Create x and b
  PetscCall(MatCreateVecs(AplusJ, &x, &b));
  PetscCall(PetscRandomCreate(PETSC_COMM_WORLD, &rctx));
  PetscCall(VecSetRandom(x, rctx));
  PetscCall(PetscRandomDestroy(&rctx));
  PetscCall(MatMult(AplusJ, x, b));

  // Compute preconditioner operators
  PetscCall(MatGetLocalSize(Acondensed, &condensed_am, NULL));
  PetscCall(MatCreate(PETSC_COMM_WORLD, &D));
  PetscCall(MatSetType(D, MATAIJ));
  PetscCall(MatSetSizes(D, condensed_am, condensed_am, PETSC_DETERMINE, PETSC_DETERMINE));
  PetscCall(MatGetOwnershipRange(D, &Dstart, &Dend));
  for (PetscInt i = Dstart; i < Dend; ++i) PetscCall(MatSetValues(D, 1, &i, 1, &i, &zero, INSERT_VALUES));
  PetscCall(MatAssemblyBegin(D, MAT_FINAL_ASSEMBLY));
  PetscCall(MatAssemblyEnd(D, MAT_FINAL_ASSEMBLY));
  PetscCall(MatCreateVecs(D, &DVec, NULL));
  PetscCall(MatGetDiagonal(AplusJ, DVec));
  PetscCall(MatDiagonalSet(D, DVec, INSERT_VALUES));
  PetscCall(MatDuplicate(Acondensed, MAT_COPY_VALUES, &AplusD));
  PetscCall(MatAXPY(AplusD, alpha, D, SUBSET_NONZERO_PATTERN));
  PetscCall(MatDuplicate(J, MAT_COPY_VALUES, &JplusD));
  PetscCall(MatAXPY(JplusD, alpha, D, SUBSET_NONZERO_PATTERN));

  // Initialize our SMW context
  ctx.U            = U;
  ctx.D            = D;
  ctx.gamma        = gamma;
  ctx.alpha        = alpha;
  ctx.setup_called = PETSC_FALSE;

  // Set preconditioner operators
  PetscCall(KSPCreate(PETSC_COMM_WORLD, &ksp));
  PetscCall(KSPSetType(ksp, KSPFGMRES));
  PetscCall(KSPSetOperators(ksp, AplusJ, AplusJ));
  PetscCall(KSPSetNormType(ksp, KSP_NORM_UNPRECONDITIONED));
  PetscCall(KSPGMRESSetRestart(ksp, 300));
  PetscCall(KSPGetPC(ksp, &pc));
  PetscCall(PCSetType(pc, PCCOMPOSITE));
  PetscCall(PCCompositeSetType(pc, PC_COMPOSITE_SPECIAL));
  PetscCall(PCCompositeAddPCType(pc, PCILU));
  PetscCall(PCCompositeAddPCType(pc, PCSHELL));
  PetscCall(PCCompositeGetPC(pc, 0, &pcA));
  PetscCall(PCCompositeGetPC(pc, 1, &pcJ));
  PetscCall(PCSetOperators(pcA, AplusD, AplusD));
  PetscCall(PCSetOperators(pcJ, JplusD, JplusD));
  PetscCall(PCShellSetContext(pcJ, &ctx));
  PetscCall(PCShellSetApply(pcJ, SmwApply));
  PetscCall(PCShellSetSetUp(pcJ, SmwSetup));
  PetscCall(PCCompositeSpecialSetAlphaMat(pc, D));

  // Solve
  PetscCall(KSPSetFromOptions(ksp));
  PetscCall(KSPSolve(ksp, b, x));

  PetscCall(MatDestroy(&A));
  PetscCall(MatDestroy(&B));
  PetscCall(MatDestroy(&Q));
  PetscCall(MatDestroy(&Acondensed));
  PetscCall(MatDestroy(&Bcondensed));
  PetscCall(MatDestroy(&BT));
  PetscCall(MatDestroy(&J));
  PetscCall(MatDestroy(&AplusJ));
  PetscCall(MatDestroy(&QInv));
  PetscCall(MatDestroy(&D));
  PetscCall(MatDestroy(&AplusD));
  PetscCall(MatDestroy(&JplusD));
  PetscCall(MatDestroy(&U));
  if (rank == 0) PetscCall(VecDestroy(&bound));
  PetscCall(VecDestroy(&x));
  PetscCall(VecDestroy(&b));
  PetscCall(VecDestroy(&Qdiag));
  PetscCall(VecDestroy(&DVec));
  PetscCall(ISDestroy(&boundary_is));
  PetscCall(ISDestroy(&bulk_is));
  PetscCall(KSPDestroy(&ksp));
  PetscCall(PetscFree(boundary_indices));
  PetscCall(MatDestroy(&ctx.UT));
  PetscCall(MatDestroy(&ctx.I_plus_gammaUTaDinvU));
  PetscCall(MatDestroy(&ctx.aD));
  PetscCall(MatDestroy(&ctx.aDinv));
  PetscCall(PCDestroy(&ctx.smw_cholesky));

  PetscCall(PetscFinalize());
  return 0;
}

/*TEST

   build:
      requires: !complex

   test:
      args: -fA ${DATAFILESPATH}/matrices/ifiss/A -fB ${DATAFILESPATH}/matrices/ifiss/B -fQ ${DATAFILESPATH}/matrices/ifiss/Q -fbound ${DATAFILESPATH}/is/ifiss/bound -ksp_monitor
      requires: datafilespath defined(PETSC_USE_64BIT_INDICES) !complex double

   test:
      suffix: 2
      nsize: 2
      args: -fA ${DATAFILESPATH}/matrices/ifiss/A -fB ${DATAFILESPATH}/matrices/ifiss/B -fQ ${DATAFILESPATH}/matrices/ifiss/Q -fbound ${DATAFILESPATH}/is/ifiss/bound -ksp_monitor
      requires: datafilespath defined(PETSC_USE_64BIT_INDICES) !complex double strumpack

TEST*/
