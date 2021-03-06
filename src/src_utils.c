/**
  * \file src_utils.c
  */

#ifdef WRAD

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "prototypes.h"
#include "oct.h"
#include "chem_utils.h"
#include "tools.h"
#include "convert.h"

#ifdef WMPI
#include <mpi.h>
#endif // WMPI

#ifdef COARSERAD
#ifdef STARS
void collectstars(struct CELL* cellcoarse, struct CELL* cell, struct RUNPARAMS* param, REAL dxcoarse, REAL aexp, REAL tcur, int* ns) {
#ifdef PIC
    REAL srcint = param->srcint;
    struct PART* nexp;
    struct PART* curp;

    // found a root
    if(cell->child == NULL) {
        nexp = cell->phead;

        if(nexp != NULL) {
            do {
                curp = nexp;
                nexp = curp->next;

                if ((curp->isStar)) {
                    (*ns)++;

                    //printf("coucou %e %e %e\n",tcur,curp->age,param->stars->tlife);
                    if(tcur >= curp->age) { // for inter-level communications
                        //printf("hehey\n");
                        if ( (tcur - curp->age) < param->stars->tlife  ) {
                            //printf("hihi %e\n",dxcoarse);
                            int igrp;

                            for(igrp = 0; igrp < NGRP; igrp++)
                                cellcoarse->rfield.src[igrp] +=  (curp->mass * param->unit.unit_mass) * srcint / POW(dxcoarse * param->unit.unit_l, 3) * param->unit.unit_t / param->unit.unit_N * POW(aexp, 2); // switch to code units
                        }
                    }
                }
            } while(nexp != NULL);
        }
    }

    else {
        // recursive call
        int icell;

        for(icell = 0; icell < 8; icell++) {
            collectstars(cellcoarse, &(cell->child->cell[icell]), param, dxcoarse, aexp, tcur, ns);
        }
    }

#endif // PIC
}

// --------------------------------------------------------------------

int putsource2coarse(struct CELL* cell, struct RUNPARAMS* param, int level, REAL aexp, REAL tcur) {
    int igrp;

    for(igrp = 0; igrp < NGRP; igrp++) {
        cell->rfield.src[igrp]   = 0.;
        cell->rfieldnew.src[igrp] = 0.;
    }

    REAL dxcoarse = POW(0.5, level);
    int ns = 0;
    collectstars(cell, cell, param, dxcoarse, aexp, tcur, &ns);

    for(igrp = 0; igrp < NGRP; igrp++) {
        cell->rfieldnew.src[igrp] = cell->rfield.src[igrp];
    }

    return ns;
}

#endif //STARS

// --------------------------------------------------------------------

void collectE(struct CELL* cellcoarse, struct CELL* cell, struct RUNPARAMS* param, int level, REAL aexp, REAL tcur, int* ns) {
    REAL fact = POW(2., 3 * (param->lcoarse - level));

    // found a root
    if(cell->child == NULL) {
        int igrp;

        for(igrp = 0; igrp < NGRP; igrp++) {
            cellcoarse->rfield.e[igrp]   += cell->rfield.e[igrp] * fact;
            cellcoarse->rfield.fx[igrp]  += cell->rfield.fx[igrp] * fact;
            cellcoarse->rfield.fy[igrp]  += cell->rfield.fy[igrp] * fact;
            cellcoarse->rfield.fz[igrp]  += cell->rfield.fz[igrp] * fact;
        }

        // below is essentially for outputs
        cellcoarse->rfield.nhplus += cell->rfield.nhplus * fact;
#ifdef HELIUM
        cellcoarse->rfield.nheplus += cell->rfield.nheplus * fact;
        cellcoarse->rfield.nhepplus += cell->rfield.nhepplus * fact;
#endif
        cellcoarse->rfield.eint += cell->rfield.eint * fact;
    }

    else {
        // recursive call
        int icell;

        for(icell = 0; icell < 8; icell++) {
            collectE(cellcoarse, &(cell->child->cell[icell]), param, level + 1, aexp, tcur, ns);
            (*ns)++;
        }
    }
}

// --------------------------------------------------------------------

int putE2coarse(struct CELL* cell, struct RUNPARAMS* param, int level, REAL aexp, REAL tcur) {
    int igrp;

    for(igrp = 0; igrp < NGRP; igrp++) {
        cell->rfield.e[igrp]   = 0.;
        cell->rfield.fx[igrp]   = 0.;
        cell->rfield.fy[igrp]   = 0.;
        cell->rfield.fz[igrp]   = 0.;
    }

    int ns = 0;
    collectE(cell, cell, param, level, aexp, tcur, &ns);

    for(igrp = 0; igrp < NGRP; igrp++) {
        cell->rfieldnew.e[igrp]    = cell->rfield.e[igrp]  ;
        cell->rfieldnew.fx[igrp]   = cell->rfield.fx[igrp] ;
        cell->rfieldnew.fy[igrp]   = cell->rfield.fy[igrp] ;
        cell->rfieldnew.fz[igrp]   = cell->rfield.fz[igrp] ;
    }

    return ns;
}


#endif //COARSERAD

#ifdef WHYDRO2
int hydro_sources(struct CELL* cell, struct RUNPARAMS* param, REAL aexp) {
    /*
     * set sources according to hydro overdensity position and temperature
     */
    int flag;

    if((cell->field.d > param->denthresh) && (cell->rfield.temp < param->tmpthresh)) {
        int igrp;

        for(igrp = 0; igrp < NGRP; igrp++) {
            REAL X0 = 1. / POW(2, param->lcoarse);
            cell->rfield.src[igrp] = param->srcint * cell->field.d / POW(X0 * param->unit.unit_l, 3) * param->unit.unit_t / param->unit.unit_N * POW(aexp, 2); // switch to code units
            cell->rfieldnew.src[igrp] = cell->rfield.src[igrp];
        }

        flag = 1;
    }

    else {
        int igrp;

        for(igrp = 0; igrp < NGRP; igrp++) {
            cell->rfield.src[igrp] = 0.;
            cell->rfieldnew.src[igrp] = 0.;
        }

        flag = 0;
    }

    return flag;
}
#endif // HYDRO2

#if defined(UVBKG) || defined(STARS_TO_UVBKG)
void uvbkg_sources(struct CELL* cell, struct RUNPARAMS* param, REAL aexp) {
    /*
     * set UV background according to the value in uvbkg.dat
     */
    int igrp;

    for(igrp = 0; igrp < NGRP; igrp++) {
        //cell->rfield.src[igrp] = param->uv.value[igrp]/param->unit.unit_N * param->unit.unit_t*POW(aexp,2);
        // printf("uvbkg_src=%e\n",cell->rfield.src[igrp]);
#ifdef STARS_TO_UVBKG
        cell->rfield.src[igrp] *= param->uv.efficiency;
#endif // STARS_TO_UVBKG
        cell->rfieldnew.src[igrp] = cell->rfield.src[igrp];
        int flag = 1;
    }
}
#endif // UVBKG

#ifdef STARS
int stars_sources(struct CELL* cell, struct RUNPARAMS* param, REAL aexp) {
    /*
     * set sources according to stars particles states and position
     */
    int igrp;

    for(igrp = 0; igrp < NGRP; igrp++) {
        cell->rfield.src[igrp] = 0.;
        cell->rfieldnew.src[igrp] = 0.;
    }

    REAL  dxcur = POW(0.5, cell2oct(cell)->level);
    int flag = 0;
    REAL srcint = param->srcint;
    struct PART* nexp = cell->phead;

    if(nexp == NULL) return 0;

    int nss = 0;

    /* // ===  AABS */
    /* REAL mstars_level=0; // mass of stars at level */
    /* REAL mlevel=0; */
    /* REAL res=param->stars->mass_res; */

    /* if(res>100){ */
    /*   mstars_level = param->stars->mass_res*SOLAR_MASS/param->unit.unit_mass; */
    /* }else{ */

    /*   mlevel=param->lcoarse; // Fix mass */
    /*   mstars_level=(param->cosmo->ob/param->cosmo->om) * POW(2.0,-3.0*(mlevel+res)) * 1E-2; */
    /*   // TODO considere ifndef TESTCOSMO */
    /* } */
    /* // ==== END AABS */

    do {
        struct PART* curp = nexp;
        nexp = curp->next;

        if ((curp->isStar >= 6)) {
#ifdef CONSTRAIN_SRC_CELL_STATE
            REAL xion = 1.;
            REAL temperature = 100000.;
            // Setting xion
            cell->rfield.nhplus = xion * cell->rfield.nh;
            cell->rfieldnew.nhplus = cell->rfield.nhplus;
            /*
                  // note below the a^5 dependance is modified to a^2 because a^3 is already included in the density
                  REAL eint=(1.5*cell->rfield.nh*KBOLTZ*(1.+xion)*temperature)*POW(aexp,2)/powf(param->unit.unit_v,2)/param->unit.unit_mass;

                  printf("phy=%e code=%e\n",(1.5*cell->rfield.nh*KBOLTZ*(1.+xion)*temperature), POW(aexp,2)/powf(param->unit.unit_v,2)/param->unit.unit_mass);

                  printf("nh=%f aexp=%f uV=%f uM=%e \n",cell->rfield.nh, aexp, param->unit.unit_v,1./param->unit.unit_mass);

                  printf("eint_init = %f eint_end = %e\n",cell->rfield.eint, eint);

                  cell->rfield.eint=eint; // 10000 K for a start
                  cell->rfieldnew.eint=eint; // 10000 K for a start

                  E2T(&cell->rfieldnew,aexp,param);
                  E2T(&cell->rfield,aexp,param);
            */
#endif

            if(curp->isStar == 100) {
                // AGN CASE
#ifdef AGN
                REAL t = (param->cosmo->tphy - curp->age) / param->stars->tlife;
                int igrp;
                REAL Ngammadot = curp->ekin / (curp->mass * param->unit.unit_mass); // here ekin contains the AGN luminosity
                Ngammadot /= (20.26 * ELECTRONVOLT); //In photons/s/kg

                for(igrp = 0; igrp < NGRP_SPACE; igrp++) {
                    cell->rfield.src[curp->radiative_state * NGRP_SPACE + igrp] +=  (curp->mass * param->unit.unit_d) * Ngammadot / POW(dxcur, 3) * param->unit.unit_t / param->unit.unit_N * POW(aexp, 2) * (t > 0 ? 1. : 0.); // switch to code units
                }

                //printf("SRC= %e\n",cell->rfield.src);
                //printf("SRC= %e t=%e tphy=%e age=%e tlife=%e\n",cell->rfield.src,t,param->cosmo->tphy,curp->age,param->stars->tlife);
                flag = 1;
#endif
            }

            else {
#ifdef DECREASE_EMMISIVITY_AFTER_TLIFE
                REAL age =  param->cosmo->tphy - curp->age;

                if (age < param->stars->tlife) {
#endif // DECREASE_EMMISIVITY_AFTER_TLIFE
                    nss++;
                    //printf("star found ! t=%e age=%e agelim=%e idx=%d COUNT=%d\n",tcur,curp->age,param->stars->tlife,curp->idx,(( (tcur - curp->age) < param->stars->tlife  )&&(tcur>= curp->age)));
                    REAL t = (param->cosmo->tphy - curp->age) / param->stars->tlife;
                    int igrp;

                    for(igrp = 0; igrp < NGRP_SPACE; igrp++) {
                        cell->rfield.src[curp->radiative_state * NGRP_SPACE + igrp] +=  (curp->mass * param->unit.unit_d) * srcint / POW(dxcur, 3) * param->unit.unit_t / param->unit.unit_N * POW(aexp, 2) * (t > 0 ? 1. : 0.); // switch to code units
                    }

                    //printf("SRC= %e\n",cell->rfield.src);
                    //printf("SRC= %e t=%e tphy=%e age=%e tlife=%e\n",cell->rfield.src,t,param->cosmo->tphy,curp->age,param->stars->tlife);
                    flag = 1;
#ifdef DECREASE_EMMISIVITY_AFTER_TLIFE

                } else {
                    /* --------------------------------------------------------------------------
                     * decreasing luminosity state, at the end of their life, star still radiate
                     * with a luminosity function of a decreasing power law of time.
                     * Slope derived from http://www.stsci.edu/science/starburst99/figs/fig77.html
                     * --------------------------------------------------------------------------
                     */
                    nss++;
                    REAL slope = -4.;
                    //printf("star found ! t=%e age=%e agelim=%e idx=%d COUNT=%d\n",tcur,curp->age,param->stars->tlife,curp->idx,(( (tcur - curp->age) < param->stars->tlife  )&&(tcur>= curp->age)));
                    REAL src = (curp->mass * param->unit.unit_d) * srcint / POW(dxcur, 3) * param->unit.unit_t / param->unit.unit_N * POW(aexp, 2);
                    REAL t = (param->cosmo->tphy - curp->age) / param->stars->tlife;
                    int igrp;

                    for(igrp = 0; igrp < NGRP_SPACE; igrp++) {
                        cell->rfield.src[curp->radiative_state * NGRP_SPACE + igrp] +=  src * (t < 1. ? 1. : POW(t, slope)) * (t > 0 ? 1. : 0);
                    }

                    //printf("SRC= %e t=%e tphy=%e age=%e tlife=%e\n",cell->rfield.src,t,param->cosmo->tphy,curp->age,param->stars->tlife);
                    flag = 1;
                }

#endif // DECREASE_EMMISIVITY_AFTER_TLIFE
            }
        }

        /* // AABS */
        /*   for(igrp=0;igrp<NGRP_SPACE;igrp++){ */
        /* 	cell->rfield.src[igrp] +=  (mstars_level*param->unit.unit_d)*srcint/POW(dxcur,3)*param->unit.unit_t/param->unit.unit_N*POW(aexp,2); // switch to code units */
        /*   } */
        /* //AABS */
    } while(nexp != NULL);

    /* else{  */

    /*   /\* // AABS *\/ */
    /*   /\*   for(igrp=0;igrp<NGRP_SPACE;igrp++){ *\/ */
    /*   /\* 	cell->rfield.src[igrp] +=  (mstars_level*param->unit.unit_d)*srcint/POW(dxcur,3)*param->unit.unit_t/param->unit.unit_N*POW(aexp,2); // switch to code units *\/ */
    /*   /\*   } *\/ */
    /* }//AABS */

    for(igrp = 0; igrp < NGRP; igrp++) {
        cell->rfieldnew.src[igrp] = cell->rfield.src[igrp];
    }

    return flag;
}
#endif // STARS

// ============================================================================================
int putsource(struct CELL* cell, struct RUNPARAMS* param, int level, REAL aexp, REAL tcur, struct OCT* curoct,  struct CPUINFO* cpu) {
    int flag = 0;
    // cleaning sources field
    int igrp;

    for(igrp = 0; igrp < NGRP; igrp++) {
        cell->rfield.src[igrp] = 0.;
        cell->rfieldnew.src[igrp] = 0.;
    }

    // ========================== FOR COSMOLOGY CASES ============================
    // ===========================================================================
#ifdef STARS_TO_UVBKG
    REAL ionisation_threshold = 0.9;
    static int previous_state;

    if(param->physical_state->mean_xion < ionisation_threshold) {
        stars_sources(cell, param, aexp);

    } else {
        if (previous_state == 0) {
            previous_state = 1;
//     homosource(param, firstoct, cpu, levext);
            REAL current_stars_sources = param->bkg;
            REAL current_uvbkg_sources = 0;
            int igrp;

            for (igrp = 0; igrp < NGRP; igrp++) {
                current_uvbkg_sources += param->uv.value[igrp];
            }

            param->uv.efficiency = current_stars_sources / current_uvbkg_sources;
        }

        uvbkg_sources(cell, param, aexp);
    }

#else
#ifdef UVBKG
    uvbkg_sources(cell, param, aexp);
#else
#ifdef STARS
    stars_sources(cell, param, aexp);
#elif WHYDRO2
    flag = hydro_sources(cell, param, aexp);
#endif
#endif // UVBKG
#endif // STARS_TO_UVBKG
    return flag;
}


#if defined(WRAD) && (defined(HOMOSOURCE) || defined(STARS_TO_UVBKG))
void homosource(struct RUNPARAMS* param, struct OCT** firstoct,  struct CPUINFO* cpu, int levext) {
    struct OCT* nextoct;
    struct OCT* curoct;
    int level;
    int icell;
    REAL bkg = 0.;
    int N = 0;

    for(level = param->lcoarse; level <= param->lmax; level++) {
        REAL  dx3 = POW(0.5, 3 * level);
        curoct = firstoct[level - 1];

        if((curoct != NULL) && (cpu->noct[level - 1] != 0)) {
            nextoct = curoct;

            do {
                curoct = nextoct;
                nextoct = curoct->next;

                if(curoct->cpu != cpu->rank) continue; // we don't update the boundary cells

                for(icell = 0; icell < 8; icell++) {
                    if(curoct->cell[icell].child == NULL) {
                        int igrp;

                        for(igrp = 0; igrp < NGRP; igrp++) {
                            bkg += curoct->cell[icell].rfield.src[igrp] * dx3;
                        }

                        if(curoct->cell[icell].rfield.src > 0) {
                            N++;
                            //printf("lev=%d src=%e N=%d cpu=%d\n",level,curoct->cell[icell].rfield.src,N,cpu->rank);
                        }
                    }
                }
            } while(nextoct != NULL);
        }
    }

#ifdef WMPI
    REAL bkgtot;
    MPI_Allreduce(&bkg, &bkgtot, 1, MPI_REEL, MPI_SUM, cpu->comm);
    bkg = bkgtot;
    int Ntot;
    MPI_Allreduce(&N, &Ntot, 1, MPI_INT, MPI_SUM, cpu->comm);
    N = Ntot;
#endif // WMPI

    if(cpu->rank == RANK_DISP) printf("Call lev=%d Homogeneous field found = %e with %d src\n", levext, bkg, N);

    param->bkg = bkg; // saving the value
}
#endif // defined

// ==================================================================================================================
void cleansource(struct RUNPARAMS* param, struct OCT** firstoct,  struct CPUINFO* cpu) {
    struct OCT* nextoct;
    struct OCT* curoct;
    int level;
    int icell;
    REAL bkg = 0.;
    int N = 0;

    for(level = param->lcoarse; level < param->lmax; level++) {
        curoct = firstoct[level - 1];

        if((curoct != NULL) && (cpu->noct[level - 1] != 0)) {
            nextoct = curoct;

            do {
                curoct = nextoct;
                nextoct = curoct->next;

                if(curoct->cpu != cpu->rank) continue; // we don't update the boundary cells

                for(icell = 0; icell < 8; icell++) {
                    int igrp;

                    for(igrp = 0; igrp < NGRP; igrp++) {
                        curoct->cell[icell].rfield.src[igrp] = 0.;
                        curoct->cell[icell].rfieldnew.src[igrp] = 0.;
                    }
                }
            } while(nextoct != NULL);
        }
    }
}

#if defined(UVBKG) || defined(STARS_TO_UVBKG)
void setUVBKG(struct RUNPARAMS* param, char* fname) {
    /**
      * Read a UV background from uvbkg.dat and store it.
      *
      * input values must be in comoving phot/s/m^3
      *
      * uvbkg.dat format:
      * 1 int N : number of samples
      * N lines of 2 floats: (redshift) and (comoving photons/s/m3)
      */
    //TODO consider NGRP>1
    //printf("Reading UVBKG from file :%s\n",fname);
#ifdef SINGLEPRECISION
    char* type = "%f \t %f";
#else
    char* type = "%lf \t %lf";
#endif // SINGLEPRECISION
    FILE* buf;
    buf = fopen(fname, "r");

    if(buf == NULL) {
        printf("ERROR : cannot open the file %s, please check\n", fname);
        abort();

    } else {
        size_t rstat = fscanf(buf, "%d", &param->uv.N);
        param->uv.redshift = (REAL*)calloc(param->uv.N, sizeof(REAL));
        param->uv.Nphot = (REAL*)calloc(param->uv.N, sizeof(REAL));
        param->uv.value = (REAL*)calloc(NGRP, sizeof(REAL));
        int i;

        for(i = 0; i < param->uv.N; i++) {
            rstat = fscanf(buf, type, &param->uv.redshift[i], &param->uv.Nphot[i]);
            //printf("%e, %e\n",param->uv.redshift[i], param->uv.Nphot[i]);
        }
    }
}

void setUVvalue(struct RUNPARAMS* param, REAL aexp) {
    /**
      * Linear fit of UV background
      *
      * Linear interpolation of the data from uvbkg.dat
      * to get a value at current aexp.
      */
    //TODO consider NGRP>1
    if(NGRP > 1) printf("WARNING BAD BEHAVIOR FOR BKG with NGRP>1 !\n");

    REAL z = 1. / aexp - 1.;

    if (z < param->uv.redshift[param->uv.N - 1]) {
        int igrp;

        for(igrp = 0; igrp < NGRP; igrp++) {
            int iredshift;

            for(iredshift = 1; iredshift < param->uv.N; iredshift++) {
                if(z <= param->uv.redshift[iredshift]) {
                    REAL y1 = param->uv.Nphot[iredshift];
                    REAL y2 = param->uv.Nphot[iredshift - 1];
                    REAL x1 = param->uv.redshift[iredshift];
                    REAL x2 = param->uv.redshift[iredshift - 1];
                    param->uv.value[igrp] = (z - x1) * (y2 - y1) / (x2 - x1) + y1;
                    //printf("interp uvbkg = %e phot/s/m3 at z=%f\n", param->uv.value[igrp], z);
                    break;
                }
            }
        }

    } else {
        int igrp;

        for(igrp = 0; igrp < NGRP; igrp++) param->uv.value[igrp] = 0.;
    }

    int igrp;
    //for(igrp=0;igrp<NGRP;igrp++)    printf("cell->rfield.src[igrp] = %f \n", param->uv.value[igrp]);
}
#endif // defined

// ============================================================================================

int FillRad(int level, struct RUNPARAMS* param, struct OCT** firstoct,  struct CPUINFO* cpu, int init, REAL aexp, REAL tloc) {
    struct OCT* nextoct;
    struct OCT* curoct;
    REAL dx;
    int icomp, icell;
    int nread;
    REAL d;
    int nc = 0;
    int igrp;
    //Anouk Inputs: aaazzzzzzzzzeeeeeeeeeeeeeeeeeeeeeeeerrrrrtyyuuuuuuuuuklmsqqqqqqqqqqqqqqqwqsqsdfghjklmwxccvb  nn&é
    int flag;
#ifdef TESTCOSMO
    //REAL tcur=a2t(param,aexp);
    REAL tcur = param->cosmo->tphy;
#else
    REAL tcur = tloc; // TODO PROBABLY NEEDS TO BE FIXED FOR NON COSMO RUN WITH STARS (LATER....)
#endif
    //if(cpu->rank==RANK_DISP) printf("Building Source field\n");
#if defined(UVBKG) || defined(STARS_TO_UVBKG)
    setUVvalue(param, aexp);
#endif
    curoct = firstoct[level - 1];

    if((curoct != NULL) && (cpu->noct[level - 1] != 0)) {
        nextoct = curoct;

        do {
            curoct = nextoct;
            nextoct = curoct->next;

            if(curoct->cpu != cpu->rank) continue; // we don't update the boundary cells

            for(icell = 0; icell < 8; icell++) {
#ifdef COARSERAD

                if(level == param->lcoarse) {
                    // WE pump up the high resolution conserved RT quantities (if the cell is split)
                    if(curoct->cell[icell].child != NULL) {
                        int nE;
                        nE = putE2coarse(&(curoct->cell[icell]), param, level, aexp, tcur);
                    }
                }

#endif
                // filling the temperature, nh, and xion
#ifdef WCHEM
#ifdef WRADHYD
                d = curoct->cell[icell].field.d; // baryonic density [unit_mass/unit_lenght^3]
                //curoct->cell[icell].rfield.nh=d/(PROTON_MASS*MOLECULAR_MU)*param->unit.unit_d; // switch to atom/m^3
                curoct->cell[icell].rfield.nh = d * (1. - YHE); // [unit_N] note d in unit_d and nh in unit_N are identical
                curoct->cell[icell].rfieldnew.nh = curoct->cell[icell].rfield.nh;
                curoct->cell[icell].rfield.eint = curoct->cell[icell].field.p / (GAMMA - 1.); // 10000 K for a start
                curoct->cell[icell].rfieldnew.eint = curoct->cell[icell].field.p / (GAMMA - 1.);
                /* curoct->cell[icell].rfieldnew.nhplus=curoct->cell[icell].field.dX/(PROTON_MASS*MOLECULAR_MU)*param->unit.unit_d; */
                /* curoct->cell[icell].rfield.nhplus=curoct->cell[icell].field.dX/(PROTON_MASS*MOLECULAR_MU)*param->unit.unit_d; */
                curoct->cell[icell].rfield.nhplus = curoct->cell[icell].field.dX; // [unit_N] note d in unit_d and nh in unit_N are identical
                curoct->cell[icell].rfieldnew.nhplus = curoct->cell[icell].rfield.nhplus; // [unit_N] note d in unit_d and nh in unit_N are identical
#ifdef HELIUM
                curoct->cell[icell].rfield.nheplus = curoct->cell[icell].field.dXHE / MHE_OVER_MH; // [unit_N] note d in unit_d and nh in unit_N are identical
                curoct->cell[icell].rfieldnew.nheplus = curoct->cell[icell].rfield.nheplus; // [unit_N] note d in unit_d and nh in unit_N are identical
                curoct->cell[icell].rfield.nhepplus = curoct->cell[icell].field.dXXHE / MHE_OVER_MH; // [unit_N] note d in unit_d and nh in unit_N are identical
                curoct->cell[icell].rfieldnew.nhepplus = curoct->cell[icell].rfield.nhepplus; // [unit_N] note d in unit_d and nh in unit_N are identical
#endif // HELIUM
#endif // WRADHYD
#endif // WCHEM

                if(cpu->trigstar) flag = putsource(&(curoct->cell[icell]), param, level, aexp, tcur, curoct, cpu); // creating sources if required

                /* if(curoct->cell[icell].rfield.src[0]>0.){ */
                /*   printf("AHAHAHAHAH\n"); */
                /*   printf("WSRC d=%e dX=%e xio=%e\n",curoct->cell[icell].rfield.nh,curoct->cell[icell].rfield.nhplus,curoct->cell[icell].rfield.nhplus/curoct->cell[icell].rfield.nh); */
                /* } */

                if(init == 1) {
#ifdef WCHEM
#ifndef COOLING
                    REAL temperature = 1e4;
                    REAL xion = 1.2e-3;
#else
                    REAL temperature = 1e2;
                    REAL xion = 1e-5;
#endif // COOLING
                    curoct->cell[icell].rfield.nhplus = xion * curoct->cell[icell].rfield.nh;
                    curoct->cell[icell].rfieldnew.nhplus = curoct->cell[icell].rfield.nhplus;
#ifdef HELIUM
                    curoct->cell[icell].rfield.nheplus = xion * curoct->cell[icell].rfield.nh * yHE;
                    curoct->cell[icell].rfieldnew.nheplus = curoct->cell[icell].rfield.nheplus;
                    curoct->cell[icell].rfield.nhepplus = xion * curoct->cell[icell].rfield.nh * yHE;
                    curoct->cell[icell].rfieldnew.nhepplus = curoct->cell[icell].rfield.nhepplus;
#endif // HELIUM
#ifndef WRADHYD
                    // note below the a^5 dependance is modified to a^2 because a^3 is already included in the density
                    REAL eint = (1.5 * curoct->cell[icell].rfield.nh * KBOLTZ * (1. + xion) * temperature) * POW(aexp, 2) / POW(param->unit.unit_v, 2) / param->unit.unit_mass;
                    curoct->cell[icell].rfield.eint = eint; // 10000 K for a start
                    curoct->cell[icell].rfieldnew.eint = eint; // 10000 K for a start
                    E2T(&curoct->cell[icell].rfieldnew, aexp, param);
                    E2T(&curoct->cell[icell].rfield, aexp, param);
#endif // WRADHYD
#endif // WCHEM
                }

#ifdef WRADHYD
                E2T(&curoct->cell[icell].rfieldnew, aexp, param);
                E2T(&curoct->cell[icell].rfield, aexp, param);
#endif // WRADHYD
                nc += flag;
            }
        } while(nextoct != NULL);
    }

#ifdef WMPI
    int nctot;
    MPI_Allreduce(&nc, &nctot, 1, MPI_INT, MPI_SUM, cpu->comm);
    nc = nctot;
#endif // WMPI
//  if(cpu->rank==RANK_DISP) printf("== SRC STAT === > Found %d sources \n",nc);
    return nc;
}
#endif
