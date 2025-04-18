!
! Test the workaround for a bug in Open MPI 2.1.1 on Ubuntu 18.04.2
! See https://lists.mcs.anl.gov/pipermail/petsc-dev/2019-July/024803.html
!
! Contributed-by:       Fabian Jakub  <Fabian.Jakub@physik.uni-muenchen.de>
program main
#include "petsc/finclude/petscdmda.h"
  use petscvec
  use petscdm
  use petscdmda
  implicit none

  PetscInt, parameter :: Ndof=1, stencil_size=1
  PetscInt, parameter :: Nx=3, Ny=3
  PetscErrorCode :: myid, commsize, ierr
  PetscScalar, pointer :: xv1d(:)
  PetscInt, pointer :: lx(:), ly(:)
  PetscMPIInt, pointer :: nb(:)

  type(tDM) :: da
  type(tVec) :: gVec!, naturalVec

  PetscCallA(PetscInitialize(PETSC_NULL_CHARACTER, ierr))
  PetscCallA(mpi_comm_rank(PETSC_COMM_WORLD, myid, ierr))
  PetscCallA(mpi_comm_size(PETSC_COMM_WORLD, commsize, ierr))

  PetscCallA(DMDACreate2d(PETSC_COMM_WORLD,DM_BOUNDARY_PERIODIC, DM_BOUNDARY_PERIODIC,DMDA_STENCIL_STAR,Nx, Ny, PETSC_DECIDE, PETSC_DECIDE, Ndof, stencil_size,PETSC_NULL_INTEGER_ARRAY, PETSC_NULL_INTEGER_ARRAY, da, ierr))
  PetscCallA(DMSetFromOptions(da, ierr))
  PetscCallA(DMSetup(da, ierr))

  PetscCallA(DMCreateGlobalVector(da, gVec, ierr))
  PetscCallA(VecGetArray(gVec, xv1d, ierr))
  xv1d(:) = real(myid, kind(xv1d))
  !print *,myid, 'xv1d', xv1d, ':', xv1d
  PetscCallA(VecRestoreArray(gVec, xv1d, ierr))

  PetscCallA(PetscObjectViewFromOptions(PetscObjectCast(gVec), PETSC_NULL_OBJECT, '-show_gVec', ierr))

  PetscCallA(DMDAGetOwnershipRanges(da, lx, ly, PETSC_NULL_INTEGER_POINTER, ierr))
  PetscCallA(DMDARestoreOwnershipRanges(da, lx, ly, PETSC_NULL_INTEGER_POINTER, ierr))
  PetscCallA(DMDAGetNeighbors(da, nb, ierr))
  PetscCallA(DMDARestoreNeighbors(da, nb, ierr))

  PetscCallA(VecDestroy(gVec, ierr))
  PetscCallA(DMDestroy(da, ierr))
  PetscCallA(PetscFinalize(ierr))
end program

!/*TEST
!
!   test:
!      nsize: 9
!      args: -show_gVec
!TEST*/
