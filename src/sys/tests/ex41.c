static char help[] = "Test PETSc integer hash set.\n\n";

#include <petsc/private/hashseti.h>
#include <petscsys.h>

#define PetscCheck(expr) do {            \
  PetscAssertFalse(PetscUnlikely(!(expr)),PETSC_COMM_SELF,PETSC_ERR_LIB, "Assertion: `%s' failed.", PetscStringize(expr));  \
} while (0)

int main(int argc,char **argv)
{
  PetscHSetI     ht = NULL, hd;
  PetscInt       n, off, array[4],na,nb,i,*marray,size;
  PetscBool      has, flag;
  PetscErrorCode ierr;

  ierr = PetscInitialize(&argc,&argv,NULL,help);if (ierr) return ierr;

  ierr = PetscHSetICreate(&ht);CHKERRQ(ierr);
  PetscCheck(ht != NULL);
  ierr = PetscHSetIGetSize(ht,&n);CHKERRQ(ierr);
  PetscCheck(n == 0);

  ierr = PetscHSetIResize(ht,0);CHKERRQ(ierr);
  ierr = PetscHSetIGetSize(ht,&n);CHKERRQ(ierr);
  PetscCheck(n == 0);

  ierr = PetscHSetIHas(ht,42,&has);CHKERRQ(ierr);
  PetscCheck(has == PETSC_FALSE);

  ierr = PetscHSetIAdd(ht,42);CHKERRQ(ierr);
  ierr = PetscHSetIGetSize(ht,&n);CHKERRQ(ierr);
  PetscCheck(n == 1);
  ierr = PetscHSetIHas(ht,42,&has);CHKERRQ(ierr);
  PetscCheck(has == PETSC_TRUE);

  ierr = PetscHSetIDel(ht,42);CHKERRQ(ierr);
  ierr = PetscHSetIGetSize(ht,&n);CHKERRQ(ierr);
  PetscCheck(n == 0);
  ierr = PetscHSetIHas(ht,42,&has);CHKERRQ(ierr);
  PetscCheck(has == PETSC_FALSE);
  ierr = PetscHSetIDel(ht,42);CHKERRQ(ierr);
  ierr = PetscHSetIDel(ht,24);CHKERRQ(ierr);

  ierr = PetscHSetIQueryAdd(ht,123,&flag);CHKERRQ(ierr);
  PetscCheck(flag == PETSC_TRUE);
  ierr = PetscHSetIQueryAdd(ht,123,&flag);CHKERRQ(ierr);
  PetscCheck(flag == PETSC_FALSE);
  ierr = PetscHSetIQueryDel(ht,123,&flag);CHKERRQ(ierr);
  PetscCheck(flag == PETSC_TRUE);
  ierr = PetscHSetIQueryDel(ht,123,&flag);CHKERRQ(ierr);
  PetscCheck(flag == PETSC_FALSE);

  ierr = PetscHSetIResize(ht,13);CHKERRQ(ierr);
  ierr = PetscHSetIGetSize(ht,&n);CHKERRQ(ierr);
  PetscCheck(n == 0);

  ierr = PetscHSetIClear(ht);CHKERRQ(ierr);
  ierr = PetscHSetIGetSize(ht,&n);CHKERRQ(ierr);
  PetscCheck(n == 0);

  ierr = PetscHSetIAdd(ht,42);CHKERRQ(ierr);
  ierr = PetscHSetIAdd(ht,13);CHKERRQ(ierr);
  ierr = PetscHSetIGetSize(ht,&n);CHKERRQ(ierr);
  PetscCheck(n == 2);

  off = 0;
  ierr = PetscHSetIGetElems(ht,&off,array);CHKERRQ(ierr);
  ierr = PetscSortInt(off,array);CHKERRQ(ierr);
  PetscCheck(off == 2);
  PetscCheck(array[0] == 13);
  PetscCheck(array[1] == 42);
  ierr = PetscHSetIGetElems(ht,&off,array);CHKERRQ(ierr);
  ierr = PetscSortInt(2,array+2);CHKERRQ(ierr);
  PetscCheck(off == 4);
  PetscCheck(array[0] == 13);
  PetscCheck(array[1] == 42);
  PetscCheck(array[0] == 13);
  PetscCheck(array[1] == 42);

  off = 0;
  ierr = PetscHSetIDuplicate(ht,&hd);CHKERRQ(ierr);
  ierr = PetscHSetIGetElems(hd,&off,array);CHKERRQ(ierr);
  ierr = PetscSortInt(off,array);CHKERRQ(ierr);
  PetscCheck(off == 2);
  PetscCheck(array[0] == 13);
  PetscCheck(array[1] == 42);
  ierr = PetscHSetIDestroy(&hd);CHKERRQ(ierr);

  ierr = PetscHSetIAdd(ht,0);CHKERRQ(ierr);
  ierr = PetscHSetIGetSize(ht,&n);CHKERRQ(ierr);
  PetscCheck(n != 0);
  ierr = PetscHSetIReset(ht);CHKERRQ(ierr);
  ierr = PetscHSetIGetSize(ht,&n);CHKERRQ(ierr);
  PetscCheck(n == 0);
  ierr = PetscHSetIReset(ht);CHKERRQ(ierr);
  ierr = PetscHSetIGetSize(ht,&n);CHKERRQ(ierr);
  PetscCheck(n == 0);
  ierr = PetscHSetIAdd(ht,0);CHKERRQ(ierr);
  ierr = PetscHSetIGetSize(ht,&n);CHKERRQ(ierr);
  PetscCheck(n != 0);

  ierr = PetscHSetIDestroy(&ht);CHKERRQ(ierr);
  PetscCheck(ht == NULL);

  ierr = PetscHSetICreate(&ht);CHKERRQ(ierr);
  ierr = PetscHSetIReset(ht);CHKERRQ(ierr);
  ierr = PetscHSetIGetSize(ht,&n);CHKERRQ(ierr);
  PetscCheck(n == 0);
  ierr = PetscHSetIDestroy(&ht);CHKERRQ(ierr);

  ierr = PetscHSetICreate(&ht);CHKERRQ(ierr);
  ierr = PetscHSetICreate(&hd);CHKERRQ(ierr);
  n = 10;
  ierr = PetscHSetIResize(ht,n);CHKERRQ(ierr);
  ierr = PetscHSetIResize(hd,n);CHKERRQ(ierr);
  ierr = PetscHSetIGetCapacity(ht,&na);CHKERRQ(ierr);
  ierr = PetscHSetIGetCapacity(hd,&nb);CHKERRQ(ierr);
  PetscCheck(na>=n);
  PetscCheck(nb>=n);
  for (i=0; i<n; i++) {
    ierr = PetscHSetIAdd(ht,i+1);CHKERRQ(ierr);
    ierr = PetscHSetIAdd(hd,i+1+n);CHKERRQ(ierr);
  }
  ierr = PetscHSetIGetCapacity(ht,&nb);CHKERRQ(ierr);
  PetscCheck(nb>=na);
  /* Merge ht and hd, and the result is in ht */
  ierr = PetscHSetIUpdate(ht,hd);CHKERRQ(ierr);
  ierr = PetscHSetIDestroy(&hd);CHKERRQ(ierr);
  ierr = PetscHSetIGetSize(ht,&size);CHKERRQ(ierr);
  PetscCheck(size==(2*n));
  ierr = PetscMalloc1(n*2,&marray);CHKERRQ(ierr);
  off = 0;
  ierr = PetscHSetIGetElems(ht,&off,marray);CHKERRQ(ierr);
  ierr = PetscHSetIDestroy(&ht);CHKERRQ(ierr);
  PetscCheck(off==(2*n));
  ierr = PetscSortInt(off,marray);CHKERRQ(ierr);
  for (i=0; i<n; i++) {
    PetscCheck(marray[i]==(i+1));
    PetscCheck(marray[n+i]==(i+1+n));
  }
  ierr = PetscFree(marray);CHKERRQ(ierr);

  ierr = PetscFinalize();
  return ierr;
}

/*TEST

   test:

TEST*/
