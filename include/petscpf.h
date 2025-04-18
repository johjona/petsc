/*
      mathematical function module.
*/
#pragma once

#include <petscvec.h>

/* MANSEC = Vec */
/* SUBMANSEC = PF */

/*
    PFList contains the list of mathematical functions currently registered
   These are added with PFRegister()
*/
PETSC_EXTERN PetscFunctionList PFList;

/*J
    PFType - Type of PETSc mathematical function, a string name

   Level: beginner

.seealso: `PFSetType()`, `PF`
J*/
typedef const char *PFType;
#define PFCONSTANT "constant"
#define PFMAT      "mat"
#define PFSTRING   "string"
#define PFQUICK    "quick"
#define PFIDENTITY "identity"
#define PFMATLAB   "matlab"

/*S
     PF - Abstract PETSc mathematical function that can be evaluated with `PFApply()` and may be constructed at run time (see `PFSTRING`)

   Level: beginner

.seealso: `PFCreate()`, `PFDestroy()`, `PFSetType()`, `PFApply()`, `PFApplyVec()`, `PFSet()`, `PFType`
S*/
typedef struct _p_PF *PF;

PETSC_EXTERN PetscClassId PF_CLASSID;

PETSC_EXTERN PetscErrorCode PFCreate(MPI_Comm, PetscInt, PetscInt, PF *);
PETSC_EXTERN PetscErrorCode PFSetType(PF, PFType, void *);
PETSC_EXTERN PetscErrorCode PFSet(PF, PetscErrorCode (*)(void *, PetscInt, const PetscScalar *, PetscScalar *), PetscErrorCode (*)(void *, Vec, Vec), PetscErrorCode (*)(void *, PetscViewer), PetscErrorCode (*)(void *), void *);
PETSC_EXTERN PetscErrorCode PFApply(PF, PetscInt, const PetscScalar *, PetscScalar *);
PETSC_EXTERN PetscErrorCode PFApplyVec(PF, Vec, Vec);

PETSC_EXTERN PetscErrorCode PFStringSetFunction(PF, const char *);

PETSC_EXTERN PetscErrorCode PFInitializePackage(void);
PETSC_EXTERN PetscErrorCode PFFinalizePackage(void);

PETSC_EXTERN PetscErrorCode PFRegister(const char[], PetscErrorCode (*)(PF, void *));

PETSC_EXTERN PetscErrorCode PFDestroy(PF *);
PETSC_EXTERN PetscErrorCode PFSetFromOptions(PF);
PETSC_EXTERN PetscErrorCode PFGetType(PF, PFType *);

PETSC_EXTERN PetscErrorCode PFView(PF, PetscViewer);
PETSC_EXTERN PetscErrorCode PFViewFromOptions(PF, PetscObject, const char[]);

#define PFSetOptionsPrefix(a, s) PetscObjectSetOptionsPrefix((PetscObject)(a), s)
