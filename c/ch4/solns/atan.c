//START
static char help[] = "Newton's method for arctan x = 0.\n\n";

#include <petsc.h>

PetscErrorCode FormFunction(SNES snes, Vec x, Vec F, void *ctx) {
    PetscErrorCode ierr;
    const PetscReal  *ax;
    PetscReal        *aF;

    ierr = VecGetArrayRead(x,&ax);CHKERRQ(ierr);
    ierr = VecGetArray(F,&aF);CHKERRQ(ierr);
    aF[0] = atan(ax[0]);
    ierr = VecRestoreArrayRead(x,&ax);CHKERRQ(ierr);
    ierr = VecRestoreArray(F,&aF);CHKERRQ(ierr);
    return 0;
}

int main(int argc,char **argv) {
    PetscErrorCode ierr;
    SNES      snes;          // nonlinear solver context
    Vec       x, r;          // solution, residual vectors
    PetscReal x0 = 2.0;

    PetscInitialize(&argc,&argv,(char*)0,help);

    ierr = PetscOptionsBegin(PETSC_COMM_WORLD,"","options to e3arctan","");CHKERRQ(ierr);
    ierr = PetscOptionsReal("-x0","initial value","",x0,&x0,NULL); CHKERRQ(ierr);
    ierr = PetscOptionsEnd();CHKERRQ(ierr);

    ierr = VecCreate(PETSC_COMM_WORLD,&x); CHKERRQ(ierr);
    ierr = VecSetSizes(x,PETSC_DECIDE,1); CHKERRQ(ierr);
    ierr = VecSetFromOptions(x); CHKERRQ(ierr);
    ierr = VecSet(x,x0); CHKERRQ(ierr);
    ierr = VecDuplicate(x,&r); CHKERRQ(ierr);

    ierr = SNESCreate(PETSC_COMM_WORLD,&snes); CHKERRQ(ierr);
    ierr = SNESSetFunction(snes,r,FormFunction,NULL); CHKERRQ(ierr);
    ierr = SNESSetFromOptions(snes); CHKERRQ(ierr);
    ierr = SNESSolve(snes,NULL,x); CHKERRQ(ierr);
    ierr = VecView(x,PETSC_VIEWER_STDOUT_WORLD); CHKERRQ(ierr);

    VecDestroy(&x);  VecDestroy(&r);  SNESDestroy(&snes);
    PetscFinalize();
    return 0;
}
//END