/**
 * \file quartz.c
 * \brief main file of EMMA
 *
 *
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <mpi.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <stdbool.h>


#include "hilbert.h"
#include "prototypes.h"
#include "io.h"
#include "restart.h"
#include "ic.h"
#include "cic.h"
#include "oct.h"
#include "particle.h"
#include "amr.h"
#include "tools.h"
#include "segment.h"
#include "communication.h"
#include "friedmann.h"
#include "parameters.h"
#include "advanceamr.h"

#ifdef WHYDRO2
#include "hydro_utils.h"
#endif // WHYDRO2

#ifdef WGRAV
#include "poisson_utils.h"
#endif // WGRAV

#ifdef WRAD
#include "rad_utils.h"
#ifdef WCHEM
#include "chem_utils.h"
#endif // WCHEM
#endif // WRAD

#ifdef WGPU
#include "interface.h"
#include "vector_gpu.h"
#include "cic_gpu.h"
#endif // WGPU

#ifdef GPUAXL
#include "interface.h"
#include "poisson_utils_gpu.h"
#include "hydro_utils_gpu.h"
#include "rad_utils_gpu.h"
#endif

#ifdef SUPERNOVAE
#include "supernovae.h"
#endif // SUPERNOVAE

#ifdef MOVIE
#include "movie.h"
#endif // MOVIE


void gdb_debug()
{
    int i = 0;
    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    printf("PID %d on %s ready for attach\n", getpid(), hostname);
    fflush(stdout);

    while (0 == i)
        sleep(5);
}

// ===============================================================================


//------------------------------------------------------------------------
// the MAIN CODE
//------------------------------------------------------------------------

#ifdef TESTCOSMO
REAL f_aexp(REAL aexp, REAL omegam, REAL omegav)
{
    return 1. / SQRT(omegam / aexp + omegav * aexp * aexp);
}
#endif

int main(int argc, char* argv[])
{
#ifdef WMPI
    MPI_Init(&argc, &argv);
    REAL tstart = MPI_Wtime();
#endif
    struct OCT* grid;
    struct OCT** firstoct;
    struct OCT** lastoct;
    int level, levelcoarse, levelmax, levelmin;
    int nvcycles;
    int nrelax;
    int ngridmax, ngrid;
    int npartmax;
    int cur, curnext; // flat indexes with boundaries
    int i, il, ichild, icell, inext, ii, ip, j;
    int xp, yp, zp;
    int NBND = 1, NBND2 = 2 * NBND;
    REAL dx;
    int vnei[6], vcell[6]; // arrays to get neighbors
    int vnei2[6], vcell2[6]; // arrays to get neighbors
    int vnei3[6], vcell3[6]; // arrays to get neighbors
    int neip[7]; // contains the index of the six neighbors of the current parent cell +current parent
    int ci, cj, ck;
    int cinext, cjnext, cknext;
    REAL threshold;
    REAL tsim = 0.;
    REAL tinit;
    struct OCT oct;
    struct OCT* nextoct;
    struct OCT* curoct;
    struct OCT* desoct;
    struct CELL* parcell;
    struct CELL* newcell;
    struct CELL* newcell2;
    int tag;
    REAL dxcur;
    REAL* dens;
    int nxoct;
    int lmap;
    int npart;
    struct PART* part;
    struct PART* nexploc, *curploc;
    struct PART* freepart;
    struct OCT* freeoct; // the first free oct available for refinement
    struct OCT* newoct;
    int nref = 0, ndes = 0;
    REAL xc, yc, zc;
    int hstride;
    int rstride;
    int gstride;
    int ncomp;
    REAL acc;
    REAL dt;
    int ntot = 0, nlev, noct;
    REAL ntotd = 0., nlevd = 0.;
    int cond1, cond2, cond3, cond4;
    REAL disp, mdisp;
    int dir;
    char filename[128];
    FILE* fd;
    struct PART* nexp;
    struct PART* nexp2;
    struct PART* curp;
    struct PART* lastpart;
    //SOCT=NULL;
    int curc;
    REAL dtnew = 0.;
    int nbnd;
    REAL x, y, z;
    REAL vx, vy, vz;
    REAL mass, mtot;
    REAL idx;
    REAL faexp, faexp2;
    REAL aexp;
    unsigned long long key;
    int nsteps;
    int nstepstart = 0;
    int ndumps = 0;
    struct CPUINFO cpu;
    struct RUNPARAMS param;
    size_t rstat;
    REAL avgdens;
    REAL tmax;
    REAL tdump, adump;
#ifdef PIC
    avgdens = 1.; //we assume a unit mass in a unit length box
#else
    avgdens = 0.;
#endif
    REAL ainit;
#ifdef TESTCOSMO
    double tab_aexp[NCOSMOTAB];
    double tab_ttilde[NCOSMOTAB];
    double tab_t[NCOSMOTAB];
    REAL amax;
    struct COSMOPARAM cosmo;
    param.cosmo = &cosmo;
#endif
    struct OUTPUTPARAM out_grid;
    param.out_grid = &out_grid;
    struct OUTPUTPARAM out_part;
    param.out_part = &out_part;
#ifdef STARS
    struct STARSPARAM stars;
    stars.n = 0;
    param.stars = &stars;
#endif
#ifdef SUPERNOVAE
    struct SNPARAM sn;
    param.sn = &sn;
    param.sn->trig_sn = 0;
#endif
#ifdef MOVIE
    struct MOVIEPARAM movie;
    param.movie = &movie;
#endif
    struct PHYSICAL_STATE physical_state;
    param.physical_state = &physical_state;
#ifdef WDBG
    gdb_debug();
#endif
    //========== RIEMANN CHECK ====================/
#ifdef WHYDRO2
    int rtag = 0;

#ifdef RIEMANN_HLLC
    rtag = 3;
#endif

    if(rtag == 0) {
        printf("ERROR : RIEMANN SOLVER NOT SELECTED\n");
        abort();
    }

#endif
    //========== TEST ZONE (IF REQUIRED)==========
    /*   printf("size =%d\n",sizeof(struct CELL)); */
    /*   printf("size =%d\n",sizeof(struct OCT)); */
    /*   abort(); */
#ifdef HELIUM
#ifndef WRADHYD
    printf("ERROR HELIUM INCLUDED WITHOUT COUPLED HYDRO\n");
    abort();
#endif
#endif
    //=========== some initial calls =============
    GetParameters(argv[1], &param); // reading the parameters file
    strcpy(param.paramrunfile, argv[1]);
#ifdef ALLOCT
    char gridoutput[512];
    strcpy(gridoutput, param.paramrunfile);
    strcat(gridoutput, ".grid_output");
    readOutputParam_grid(gridoutput, &param);
    char partoutput[512];
    strcpy(partoutput, param.paramrunfile);
    strcat(partoutput, ".part_output");
    readOutputParam_part(partoutput, &param);
#endif // ALLOCT
//test rdm()
//for (i=0;i<10;i++) printf("%le\n",(REAL)rdm(0,1));
//abort();
#ifdef MOVIE
    init_movie(&param);
#endif
#ifndef TESTCOSMO
    tmax = param.tmax;
#else
    //in  cosmo case tmax is understood as a maximal expansion factor
    amax = param.tmax;
#endif
#ifdef WMPI
    MPI_Datatype MPI_PACKET;
    MPI_Datatype MPI_PART;
    MPI_Datatype MPI_WTYPE;
    MPI_Datatype MPI_HYDRO;
    MPI_Datatype MPI_RTYPE;
    MPI_Datatype MPI_RAD;
    init_MPI(&cpu, &MPI_PACKET, &MPI_PART, &MPI_WTYPE, &MPI_HYDRO, &MPI_RTYPE, &MPI_RAD);
#else
    cpu->rank = 0;
    cpu->nproc = 1;
#endif // WMPI

    if(cpu.rank == RANK_DISP) {
        printf("================================\n");
        printf("            EMMA V1.2           \n");
        printf("      Engines Are Running on    \n");
        printf("             %d process         \n", cpu.nproc);
        printf("================================\n");
        copy_file(param.paramrunfile, "data/param.run");
    }

#ifdef TESTCOSMO
    //reading outputlist
    char outputlist[512];
    strcpy(outputlist, param.paramrunfile);
    strcat(outputlist, ".list_aexp");
    FILE* foutputs;
    param.aexpdump = 0;
    float tempa;

    if((foutputs = fopen(outputlist, "r")) != NULL) {
        int dump = fscanf(foutputs, "%e", &tempa);
        param.aexpdump = tempa;

        if(cpu.rank == RANK_DISP) {
            printf("Reading outputs from %s : first dump at aexp=%e\n", outputlist, param.aexpdump);
        }
    }

    else {
        if(cpu.rank == RANK_DISP)
            printf("WARNING NOT OUTPUT LIST FOUND !! \n");
    }

#endif
    //=========== assigning values =============
    levelcoarse = param.lcoarse;
    levelmax = param.lmax;
    levelmin = param.mgridlmin;
    nvcycles = param.nvcycles;
    nrelax = param.nrelax;
    ngridmax = param.ngridmax;
#ifdef PIC
    npartmax = param.npartmax;
    npart = 128 * 128 * 128;
#endif
    threshold = param.amrthresh0;
#ifdef TESTCOSMO
    threshold *= POW(2.0, -3.0 * param.lcoarse);
    param.amrthresh = threshold;
#endif
    gstride = FMAX(8, param.gstride); //POW(2,levelcoarse);
    hstride = FMAX(8, param.hstride); //POW(2,levelcoarse);
    rstride = hstride;
    ncomp = 10;
    acc = param.poissonacc;
    dt = param.dt;
    cpu.maxhash = param.maxhash;
    cpu.levelcoarse = levelcoarse;
#ifdef GPUAXL
    cpu.nthread = param.nthread;
    cpu.nstream = param.nstream;
#endif
    //breakmpi();
    /* // RDM TEST */
    /* int ir; */
    /* int nr=10; */
    /* REAL *toto; */
    /* toto=(REAL *)malloc(nr*sizeof(REAL)); */
    /* for(ir=0;ir<nr;ir++){ */
    /*   toto[ir]=rdm(0.,10.); */
    /* } */
    /* printf("cpu=%d ",cpu.rank); */
    /* for(ir=0;ir<nr;ir++){ */
    /*   printf("%e ",toto[ir]); */
    /* } */
    /* printf("\n"); */
    /* MPI_Barrier(cpu.comm); */
    /* abort(); */
    //========== allocations ===================
    //  if(cpu.rank==RANK_DISP) printf("Allocating %f GB cell=%f GB part=%f GB book=%f",(sizeof(struct OCT)*ngridmax+sizeof(struct PART)*npart+cpu.maxhash*sizeof(struct OCT*)+stride*ncomp*sizeof(REAL))/(1024*1024*1024.),sizeof(struct OCT)*ngridmax/(1024*1024*1024.),sizeof(struct PART)*npart/(1024*1024*1024.),(cpu.maxhash*sizeof(struct OCT*)+stride*ncomp*sizeof(REAL))/(1024.*1024.*1024.));
    int memsize = 0.;
    grid = (struct OCT*)calloc(ngridmax, sizeof(struct OCT));
    memsize += ngridmax * sizeof(struct OCT); // the oct grid
    cpu.locNoct =	(int*)calloc(levelmax, sizeof(int));
    memsize += levelmax * sizeof(int);			// the local number of octs per level
    cpu.octList = (struct OCT***)calloc(levelmax, sizeof(struct OCT**));
    memsize += levelmax * sizeof(struct OCT**);
    int iLev;

    for(iLev = 0; iLev < levelcoarse; iLev++) {
        //cpu.locNoct[iLev] = POW(2,3*(iLev+1));
        cpu.locNoct[iLev] = (pow(2, 3 * (iLev + 1)) < ngridmax ? pow(2, 3 * (iLev + 1)) : ngridmax) ;
        cpu.octList[iLev] = (struct OCT**)calloc(cpu.locNoct[iLev], sizeof(struct OCT*));
        memsize += ngridmax * sizeof(struct OCT**);
    }

    for(iLev = levelcoarse; iLev < levelmax; iLev++) {
        cpu.octList[iLev] = (struct OCT**)calloc(ngridmax, sizeof(struct OCT*));
        memsize += ngridmax * sizeof(struct OCT**);
    }

    int ncellscoarse = POW(2, 3 * param.lcoarse) / 8; // number of cells before refinement
    int ncellsmax    = (levelmax > levelcoarse ? 3 : 1) * ncellscoarse; 		 // max number of cells after refinement
    int lbfg = 2; 				 // load balancing factor for the grid
    int noon = (ncellsmax * lbfg) / cpu.nproc;	 // number of octs needed

    if (ngridmax < noon && cpu.rank == RANK_DISP ) {
        printf("\n");
        printf("YOU MAY NEED MORE MEMORY SPACE TO COMPUTE THE GRID\n");
        printf("%d oct allocated per processor \n", ngridmax);
        printf("%d oct approximately needed\n", noon);
        printf("\n");
    }

#ifdef PIC
    part = (struct PART*)calloc(npartmax, sizeof(struct PART));
    memsize += npartmax * sizeof(struct PART); // the particle array
    //printf("PART=== %p \n",part);
    cpu.firstpart = part;

    // we set all the particles mass to -1
    for(ii = 0; ii < npartmax; ii++) part[ii].mass = -1.0;

    int lbfp = 10;				// load balancing factor for the part
    int nopn = (ncellscoarse * lbfp) / cpu.nproc ;	// number of part needed
#ifdef STARS
    nopn *= 2;					// you can create as many stars as initial DM particles
#endif

    if (npartmax < nopn && cpu.rank == RANK_DISP ) {
        printf("\n");
        printf("YOU MAY NEED MORE MEMORY SPACE TO COMPUTE THE PARTICLE\n");
        printf("%d part allocated per processor \n", npartmax);
        printf("%d part approximately needed\n", nopn);
        printf("\n");
    }

#endif

    if(cpu.rank == RANK_DISP) {
        printf(" === alloc Memory ===\n");
        printf(" oct size=%f ngridmax=%d\n", sizeof(struct OCT) / 1024. / 1024., ngridmax);
        printf(" grid = %f MB\n", (ngridmax / (1024 * 1024.))*sizeof(struct OCT));
#ifdef PIC
        printf(" part = %f MB\n", (npartmax / (1024 * 1024.))*sizeof(struct PART));
#endif
    }

    firstoct =	(struct OCT**)calloc(levelmax, sizeof(struct OCT*));
    memsize += levelmax * sizeof(struct OCT*);		// the firstoct of each level
    lastoct =	    (struct OCT**)calloc(levelmax, sizeof(struct OCT*));
    memsize += levelmax * sizeof(struct OCT*);		// the last oct of each level
    cpu.htable =	(struct OCT**) calloc(cpu.maxhash, sizeof(struct OCT*));
    memsize += cpu.maxhash * sizeof(struct OCT*);	// the htable keys->oct address
    cpu.noct =	(int*)calloc(levelmax, sizeof(int));
    memsize += levelmax * sizeof(int);			// the number of octs per level
    cpu.npart =	(int*)calloc(levelmax, sizeof(int));
    memsize += levelmax * sizeof(int);			// the number of particles* per level	*(DM+stars ifdef STARS)
#ifdef STARS
    cpu.nstar =	(int*)calloc(levelmax, sizeof(int));
    memsize += levelmax * sizeof(int);			// the number of stars per level
    cpu.trigstar = 0;
    //  srand(time(NULL));
    srand(SEED);
#endif // STARS
#ifndef PIC
    part = NULL;
#endif
    lastpart = part - 1; // the last particle points before the first at the very beginning
    //===================================================================================================
    // allocating the vectorized tree
    // allocating the 6dim stencil
    struct HGRID* stencil;
    struct STENGRAV gstencil;
    struct RGRID* rstencil;
    //printf("stencil=%p with stride=%d\n",stencil,hstride);
    stencil = (struct HGRID*)calloc(hstride, sizeof(struct HGRID));
    //printf("stenci=%p mem=%f\n",stencil,hstride*sizeof(struct HGRID)/(1024.*1024.));
#ifdef WRAD
    rstencil = (struct RGRID*)calloc(hstride, sizeof(struct RGRID));
#endif
    struct GGRID* grav_stencil;
    grav_stencil = (struct GGRID*)calloc(gstride, sizeof(struct GGRID));
    gstencil.stencil = grav_stencil;
    gstencil.res = (REAL*)calloc(gstride * 8, sizeof(REAL));
    gstencil.pnew = (REAL*)calloc(gstride * 8, sizeof(REAL));
    gstencil.resLR = (REAL*)calloc(gstride, sizeof(REAL));
#ifdef GPUAXL
    // ================================== GPU ALLOCATIONS ===============
    countdevices(0);
    initlocaldevice(0, 1);
    checkdevice(0);
    // FOR THE MOMENT: GPU POISSON IS DISABLED, HENCE NO NEED FOR ALLOCATIONS on GPU
#ifdef WGRAV
    //create_pinned_gravstencil(&gstencil,gstride);
    /*   create_gravstencil_GPU(&cpu,gstride); */
    /*   cpu.gresA=GPUallocREAL(gstride*8); */
    /*   cpu.gresB=GPUallocREAL(gstride*8); */
    /*   cpu.cuparam=GPUallocScanPlan(gstride*8); */
#endif
#ifdef WMPI
    MPI_Barrier(cpu.comm);

    if(cpu.rank == RANK_DISP) printf("gpu alloc Poisson done\n");

#endif
#ifdef WHYDRO2
    //stencil=(struct HGRID*)calloc(hstride,sizeof(struct HGRID));
    //printf("hstencil=%p mem=%f mem/elem=%f \n",stencil,hstride*sizeof(struct HGRID)/(1024.*1024.),sizeof(struct HGRID)/(1024.*1024.));
    // UNCOMMENT BELOW FOR FASTHYDRO GPU
    stencil = NULL;
    create_pinned_stencil(&stencil, hstride);
    create_hydstencil_GPU(&cpu, hstride);
#endif
#ifdef WMPI
    MPI_Barrier(cpu.comm);

    if(cpu.rank == RANK_DISP) printf("gpu alloc hydro done\n");

#endif
#ifdef WRAD
    //rstencil=(struct RGRID*)calloc(rstride,sizeof(struct RGRID));
    //printf("rstencil=%p mem=%f\n",rstencil,rstride*sizeof(struct RGRID)/(1024.*1024.));
    // UNCOMMENT BELOW FOR FASTRT GPU
    rstencil = NULL;
    create_pinned_stencil_rad(&rstencil, rstride);
    create_radstencil_GPU(&cpu, rstride);
#endif
#ifdef WMPI
    MPI_Barrier(cpu.comm);

    if(cpu.rank == RANK_DISP) printf("gpu alloc rad done\n");

#endif
    // ====================END GPU ALLOCATIONS ===============
#endif

    if(cpu.rank == RANK_DISP) printf("Allocations %f GB done\n", memsize / (1024.*1024 * 1024));

    //========== setting up the parallel topology ===
    cpu.nsend = NULL;
    cpu.nrecv = NULL;
    cpu.nsend_coarse = NULL;
    cpu.nrecv_coarse = NULL;
    // We segment the oct distributions at levelcoarse
    cpu.bndoct = NULL;
    cpu.mpinei = NULL;
    cpu.dict = NULL;
    cpu.nbuff = param.nbuff;
    cpu.nbufforg = param.nbuff;
    cpu.bndoct = (struct OCT**)calloc(cpu.nbufforg, sizeof(struct OCT*));
    cpu.allkmin = (int*)calloc(cpu.nproc, sizeof(int));
    cpu.allkmax = (int*)calloc(cpu.nproc, sizeof(int));
    load_balance(levelcoarse, &cpu);
#ifdef WMPI
    MPI_Allgather(&cpu.kmin, 1, MPI_INT, cpu.allkmin, 1, MPI_INT, cpu.comm);
    MPI_Allgather(&cpu.kmax, 1, MPI_INT, cpu.allkmax, 1, MPI_INT, cpu.comm);
    MPI_Barrier(cpu.comm);
#else
    cpu.allkmin[0] = cpu.kmin;
    cpu.allkmax[0] = cpu.kmax;
#endif
    // =================== AGN ALLOCATIONS
#ifdef AGN
    struct AGNPARAM agn;
    agn.feedback_frac = 0.01;
    agn.x = (REAL*)calloc(param.npartmax, sizeof(REAL));
    agn.y = (REAL*)calloc(param.npartmax, sizeof(REAL));
    agn.z = (REAL*)calloc(param.npartmax, sizeof(REAL));
    //if(cpu.rank==0) printf("%d %d %p %p %p\n",cpu.rank,param.npartmax,agn.x,agn.y,agn.z);
    param.agn = &agn;
#endif
    //========== building the initial meshes ===
    struct CELL root;
    root = build_initial_grid(grid, firstoct, lastoct, &cpu, &param);
// ==================================== assigning CPU number to levelcoarse OCTS // filling the hash table // Setting up the MPI COMMS
#ifdef WMPI

    if(cpu.rank == RANK_DISP) printf("Set up MPI \n");

    cpu.sendbuffer = NULL;
    cpu.recvbuffer = NULL;
    cpu.psendbuffer = NULL;
    cpu.precvbuffer = NULL;
    cpu.hsendbuffer = NULL;
    cpu.hrecvbuffer = NULL;
    cpu.Rsendbuffer = NULL;
    cpu.Rrecvbuffer = NULL;
#endif
    int newloadb = 1;
    setup_mpi(&cpu, firstoct, levelmax, levelcoarse, ngridmax, newloadb); // out of WMPI to compute the hash table
    newloadb = 0;

    for (level = 1; level <= param.lmax; level++) {
        setOctList(firstoct[level - 1], &cpu, &param, level);
    }

#if defined(MPIIO) || defined(HDF5)
    set_offset(&param, &cpu);
    dump_domain(&param, &cpu);
#endif // defined
#if 0
#ifdef WMPI
    // allocating the communication buffers
    sendbuffer = (struct PACKET**)(calloc(cpu.nnei, sizeof(struct PACKET*)));
    recvbuffer = (struct PACKET**)(calloc(cpu.nnei, sizeof(struct PACKET*)));

    for(i = 0; i < cpu.nnei; i++) {
        sendbuffer[i] = (struct PACKET*) (calloc(cpu.nbuff, sizeof(struct PACKET)));
        recvbuffer[i] = (struct PACKET*) (calloc(cpu.nbuff, sizeof(struct PACKET)));
    }

    cpu.sendbuffer = sendbuffer;
    cpu.recvbuffer = recvbuffer;
#ifdef PIC
    psendbuffer = (struct PART_MPI**)(calloc(cpu.nnei, sizeof(struct PART_MPI*)));
    precvbuffer = (struct PART_MPI**)(calloc(cpu.nnei, sizeof(struct PART_MPI*)));

    for(i = 0; i < cpu.nnei; i++) {
        psendbuffer[i] = (struct PART_MPI*) (calloc(cpu.nbuff, sizeof(struct PART_MPI)));
        precvbuffer[i] = (struct PART_MPI*) (calloc(cpu.nbuff, sizeof(struct PART_MPI)));
    }

    cpu.psendbuffer = psendbuffer;
    cpu.precvbuffer = precvbuffer;
#endif
#ifdef WHYDRO2
    hsendbuffer = (struct HYDRO_MPI**)(calloc(cpu.nnei, sizeof(struct HYDRO_MPI*)));
    hrecvbuffer = (struct HYDRO_MPI**)(calloc(cpu.nnei, sizeof(struct HYDRO_MPI*)));

    for(i = 0; i < cpu.nnei; i++) {
        hsendbuffer[i] = (struct HYDRO_MPI*) (calloc(cpu.nbuff, sizeof(struct HYDRO_MPI)));
        hrecvbuffer[i] = (struct HYDRO_MPI*) (calloc(cpu.nbuff, sizeof(struct HYDRO_MPI)));
    }

    cpu.hsendbuffer = hsendbuffer;
    cpu.hrecvbuffer = hrecvbuffer;
#endif
#ifdef WRAD
    Rsendbuffer = (struct RAD_MPI**)(calloc(cpu.nnei, sizeof(struct RAD_MPI*)));
    Rrecvbuffer = (struct RAD_MPI**)(calloc(cpu.nnei, sizeof(struct RAD_MPI*)));

    for(i = 0; i < cpu.nnei; i++) {
        Rsendbuffer[i] = (struct RAD_MPI*) (calloc(cpu.nbuff, sizeof(struct RAD_MPI)));
        Rrecvbuffer[i] = (struct RAD_MPI*) (calloc(cpu.nbuff, sizeof(struct RAD_MPI)));
    }

    cpu.Rsendbuffer = Rsendbuffer;
    cpu.Rrecvbuffer = Rrecvbuffer;
#endif // WRAD
#endif // WMPI
#endif // 0
    // =====================  computing the memory location of the first freeoct and linking the freeocts
    freeoct = lastoct[levelcoarse - 1] + 1; //(at this stage the memory is perfectly aligned)
    freeoct->prev = NULL;
    freeoct->next = freeoct + 1;

    for(curoct = freeoct + 1; curoct < (grid + ngridmax); curoct++) { // sweeping all the freeocts
        curoct->prev = curoct - 1;
        curoct->next = NULL;

        if(curoct != (grid + ngridmax - 1)) curoct->next = curoct + 1;
    }

    //=================================  building the array of timesteps
    REAL* adt;
    adt = (REAL*)malloc(sizeof(REAL) * levelmax);

    for(level = 1; level <= levelmax; level++) adt[level - 1] = param.dt;

#ifdef COARSERAD
    REAL* adt_rad;
    adt_rad = (REAL*)malloc(sizeof(REAL) * levelmax);

    for(level = 1; level <= levelmax; level++) adt_rad[level - 1] = param.dt;

#endif
    int* ndt;
    ndt = (int*)malloc(sizeof(int) * levelmax);

    // INITIALISATION FROM INITIAL CONDITIONS =========================
    if(param.nrestart == 0) {
#ifdef PIC
        //===================================================================================================================================
        //===================================================================================================================================
        //===================================================================================================================================
        //===================================================================================================================================
        //===================================================================================================================================
        //===================================================================================================================================
        //===================================================================================================================================
        //===================================================================================================================================
        //===================================================================================================================================
        //===================================================================================================================================

        // ==================================== assigning particles to cells
        //breakmpi();
        if(cpu.rank == RANK_DISP) printf("==> starting part\n");

        // initialisation of particles

#ifdef TESTPLUM
        int dummy;
        REAL dummyf;
        int npartf;
        //breakmpi();
        fd = fopen("utils/data.inp", "r");

        if(fd == NULL) {
            printf("Error while reading particle file ABORT\n");
            abort();
        }

        fscanf(fd, "%d", &dummy);
        fscanf(fd, "%d", &npartf);
        fscanf(fd, "%f", &dummyf);
        ip = 0.;

        for(i = 0; i < npartf; i++)
        {
            //fscanf(fd,"%d %f %f %f %f %f %f %f",&part[i].idx,&part[i].mass,&(part[i].x),&(part[i].y),&(part[i].z),&(part[i].vx),&(part[i].vy),&(part[i].vz));
            fscanf(fd, "%d %f %f %f %f %f %f %f", &dummy, &mass, &x, &y, &z, &vx, &vy, &vz);
            x += 0.5;
            y += 0.5;
            z += 0.5;
            // periodic boundary conditions
            x += (x < 0) * ((int)(-x) + 1) - (x > 1.) * ((int)x);
            y += (y < 0) * ((int)(-y) + 1) - (y > 1.) * ((int)y);
            z += (z < 0) * ((int)(-z) + 1) - (z > 1.) * ((int)z);

            // it it belongs to the current cpu, we proceed and assign the particle to the particle array
            if(segment_part(x, y, z, &cpu, levelcoarse)) {
                part[ip].x = x;
                part[ip].y = y;
                part[ip].z = z;
                part[ip].vx = vx;
                part[ip].vy = vy;
                part[ip].vz = vz;
                part[ip].mass = mass;
                lastpart = part + ip;
                ip++;
            }
        }

        fclose(fd);
        npart = ip; // we compute the localnumber of particle
#endif
#ifdef TESTCOSMO // =================PARTICLE COSMOLOGICAL CASE
#ifdef HPC
        long dummy;
#else
        int dummy;
#endif
        REAL dummyf;
        int npartf;
        int nploc;
        REAL munit;
        REAL lbox;
#ifdef GRAFIC // ==================== read grafic file
#ifdef SPLIT
        lastpart = read_split_grafic_part(part, &cpu, &munit, &ainit, &npart, &param, param.lcoarse);
#ifdef WMPI
        long ntotsplit = npart;
        MPI_Allreduce(MPI_IN_PLACE, &ntotsplit, 1, MPI_LONG, MPI_SUM, cpu.comm);

        if(cpu.rank == RANK_DISP) printf("found %ld particles among the split ICs files\n", ntotsplit);

#endif
#else
        lastpart = read_grafic_part(part, &cpu, &munit, &ainit, &npart, &param, param.lcoarse);
#endif
#endif
#endif

        // we set all the "remaining" particles mass to -1
        for(ii = npart; ii < npartmax; ii++) part[ii].mass = -1.0;

        /// assigning PARTICLES TO COARSE GRID
        if(cpu.rank == RANK_DISP) printf("start populating coarse grid with particles\n");

#ifdef SPLIT
        part2gridsplit(part, &cpu, npart);
        // part2grid(part,&cpu,npart);
#else
        part2grid(part, &cpu, npart);
#endif
        // ========================  computing the memory location of the first freepart and linking the free parts
        freepart = lastpart + 1; // at this stage the memory is perfectly aligned
        freepart->prev = NULL;
        freepart->next = freepart + 1;

        for(curp = freepart + 1; curp < (part + npartmax); curp++) {
            curp->prev = curp - 1;
            curp->next = NULL;

            if(curp != (part + npartmax - 1)) curp->next = curp + 1;
        }

#endif // PIC
        //===================================================================================================================================
        //===================================================================================================================================
        //===================================================================================================================================
        //===================================================================================================================================
        //===================================================================================================================================
        //===================================================================================================================================
        //===================================================================================================================================
        //===================================================================================================================================
        //===================================================================================================================================
        //===================================================================================================================================
#ifdef WHYDRO2
#ifdef GRAFIC
        int ncellhydro;
#ifdef SPLIT
        ncellhydro = read_split_grafic_hydro(&cpu, &ainit, &param, param.lcoarse);
#else
        ncellhydro = read_grafic_hydro(&cpu, &ainit, &param, param.lcoarse);
#endif

        if(cpu.rank == RANK_DISP) printf("%d hydro cell found in grafic file with aexp=%e\n", ncellhydro, ainit);

#else
#endif // GRAFIC
#endif // WHYDRO2
        //===================================================================================================================================
#ifdef WRAD
#ifdef WCHEM
        int flag = 0;

        if(NGRP_SPACE != param.atomic.ngrp_space) {
            printf("NGRP_SPACE and NGRP_ATOMIC INCONSISTENT ! ERROR !\n");
            printf("check src/param.h and the atomic file (in src/atomic_data) chosen in param.run\n");
            flag = 1;
        }

        if(NGRP_TIME != param.atomic.ngrp_time) {
            printf("NGRP_TIME and NGRP_ATOMIC INCONSISTENT ! ERROR !\n");
            printf("check src/param.h and the atomic file (in src/atomic_data) chosen in param.run\n");
            flag = 1;
        }

        if (flag) abort();

#endif // WCHEM
#endif // WRAD
#ifdef TEST_STAR_FORMATION
        init_star_test(&param, firstoct);
        tinit = tsim;
#endif // TEST_STAR_FORMATION
        // saving the absolute initial time
#ifdef TESTCOSMO
        tinit = ainit;
#else
        tinit = tsim;
#endif
        tsim = tinit;
    }

    else {
        //==================================== Restart =================================================
#ifdef WMPI
        MPI_Barrier(cpu.comm);
#endif

        if(cpu.rank == RANK_DISP)
            printf("Restarting from snap #%d\n", param.nrestart);

#ifdef PIC
        sprintf(filename, "data/bkp/part.%05d.p%05d", param.nrestart, cpu.rank);
        freepart = restore_part(filename, firstoct, &tsim, &param, &cpu, part);
        cpu.freepart = freepart;
#endif
        sprintf(filename, "data/bkp/grid.%05d.p%05d", param.nrestart, cpu.rank);
        freeoct = restore_amr(filename, firstoct, lastoct, &tsim, &tinit, &nstepstart, &ndumps, &param, &cpu, part, adt, &root);
        cpu.freeoct = freeoct;
        nstepstart += 1.; // next timestep is n+1
        ndumps += 1.;  // next timestep is n+1

        if(cpu.rank == RANK_DISP) {
            printf(" ... Restarting from file #%d with nstep=%d tsim=%e ndumps=%d\n", param.nrestart, nstepstart, tsim, ndumps);
        }

#ifdef TESTCOSMO

        // prepare the next in aexplist
        if(param.aexpdump) {
            while(param.aexpdump <= tsim) {
                if(fscanf(foutputs, "%e", &tempa) == EOF) {
                    param.aexpdump = 0;
                    break;
                }

                else {
                    param.aexpdump = tempa;
                }
            }
        }

        if(cpu.rank == RANK_DISP) {
            printf("Next dump in the list at aexp=%e\n", param.aexpdump);
        }

#endif
#ifdef TESTCOSMO
        // temporal boundaries of the full run
        ainit = tinit;
#endif
#ifdef WMPI
        MPI_Barrier(cpu.comm);
#endif
        //==================================== END Restart =================================================
    }

    //===================================================================================================================================
    //===================================================================================================================================
    //===================================================================================================================================
    //===================================================================================================================================
    //===================================================================================================================================
    //===================================================================================================================================
    //===================================================================================================================================
    //===================================================================================================================================
    //===================================================================================================================================
    //===================================================================================================================================
#ifdef TESTCOSMO
    // ================== COMPUTATION OF FRIEDMANN TABLES
    REAL treal, treal0, trealBB;
    // we compute the friedmann tables
    aexp = tsim;
    // at this stage we have to compute the conformal time
    tsim = -0.5 * SQRT(cosmo.om) * integ_da_dt_tilde(aexp, 1.0, cosmo.om, cosmo.ov, 1e-8);
    // real times in units of 1./H0
    treal = -integ_da_dt(aexp, 1.0, cosmo.om, cosmo.ov, 1e-8);
    trealBB = -integ_da_dt(1e-5, 1.0, cosmo.om, cosmo.ov, 1e-8);
    treal0 = treal;
    // interpolation table
#ifdef WMPI
    MPI_Barrier(cpu.comm);
#endif

    if(cpu.rank == RANK_DISP) printf("computing friedmann tables with ainit=%e amax=%e\n", ainit, amax);

    compute_friedmann(ainit * 0.95, amax, NCOSMOTAB, cosmo.om, cosmo.ov, tab_aexp, tab_ttilde, tab_t);
    tmax = -0.5 * SQRT(cosmo.om) * integ_da_dt_tilde(amax, 1.0 + 1e-6, cosmo.om, cosmo.ov, 1e-8);

    if(cpu.rank == RANK_DISP) printf("tmax=%e treal=%e\n", tmax, treal);

    cosmo.tab_aexp = (REAL*)tab_aexp;
    cosmo.tab_ttilde = (REAL*)tab_ttilde;
#endif
    param.time_max = tmax;
    mkdir("data/", 0755);

    if(cpu.rank == RANK_DISP) dumpHeader(&param, &cpu, argv[1]);

    // test if each cpu will have at least one oct in the minimum level of multigrid
    int Lmin = 1 + ceil(log(cpu.nproc) / (3 * log(2.)));

    if( param.mgridlmin > 0 && param.mgridlmin < Lmin ) {
        param.mgridlmin = Lmin;

        if(cpu.rank == RANK_DISP) {
            printf("Conflict between mgridlmin and ncpu : mgridlmin set to %d\n", param.mgridlmin );
        }
    }

    //#ifdef STARS
    //if(cpu.rank==RANK_DISP) printf("mstars set to %e\n",param.stars->mstars);
    /* param.srcint *= param.stars->mstars * param.unit.unit_mass; */
    /* if(cpu.rank==RANK_DISP) printf("srcint set to %e\n",param.srcint); */
    /* param.stars->Esnfb = param.stars->mstars * param.unit.unit_mass * SN_EGY * param.stars->feedback_eff; // [J] */
    /* if(cpu.rank==RANK_DISP) printf("Esnfb set to %e\n",param.stars->Esnfb); */
    //#endif
#ifdef STARS

    if(cpu.rank == RANK_DISP) {
        for(level = 7; level < 13; level++) {
            REAL egy = 9.68e11;// J per stellar kg
            REAL dv = POW(0.5, 3 * level); //code unit
            REAL mass = (param.cosmo->ob / param.cosmo->om) * POW(0.5, 3.0 * (param.lcoarse + param.stars->mass_res));
            egy *= POW(aexp, 5) * param.unit.unit_l / (param.unit.unit_n * param.unit.unit_t); //code unit
            REAL E  = mass * egy / dv ; //code unit
            printf("E=%e ,dv=%e, mass=%e aexp=%e\n", E, dv, mass, aexp);
        }
    }

#endif // STARS
    //  abort();
    //================================================================================
    //================================================================================
    //================================================================================
    //
    //          AT THIS STAGE THE INITIAL SETUP HAS BEEN COMPLETED
    //
    //================================================================================
    //================================================================================
    //================================================================================
    int pass;
    int smark;
    int ismooth, nsmooth = 2;
    int marker;
    FILE* fegy;
    //==================================== MAIN LOOP ================================================
    //===============================================================================================
    // preparing freeocts
    cpu.freeoct = freeoct;
#ifdef PIC
    // preparing free part
    cpu.freepart = freepart;
    cpu.lastpart = part + npartmax - 1;
#endif
#ifdef GPUAXL
    // creating params on GPU
#ifdef WRAD
    create_param_GPU(&param, &cpu);
    // Note : present only in radiation routines but could be useful elsewhere ... to check
#endif
#endif
    // preparing energy stats
    //if(cpu.rank==RANK_DISP) param.fpegy=fopen("energystat.txt","w");
#ifdef TESTCOSMO
    tdump = (REAL)interp_aexp(tsim, (double*)cosmo.tab_aexp, (double*)cosmo.tab_ttilde);
#else
    tdump = tsim;
#endif
    // dumping ICs
    cpu.ndumps = &ndumps; // preparing the embedded IO
    cpu.tinit = tinit;
    int* ptot = (int*)calloc(2, sizeof(int));
    mtot = multicheck(firstoct, ptot, param.lcoarse, param.lmax, cpu.rank, &cpu, &param, 0);
    // ===== SOME BOOKEEPING FOR SPLIT INITIAL CONDITIONS
#ifdef SPLIT
#ifdef WMPI
    int deltan[2];
    int ntotsplit;
    //reset the setup in case of refinement
    //printf("2 next=%p on proc=%d\n",firstoct[0]->next,cpu->rank);
    L_partcellreorg(param.lcoarse, firstoct); // reorganizing the particles of the level throughout the mesh
    /* ptot[0]=0; */
    /* ptot[1]=0; */
    /* for(ip=1;ip<=param.lmax;ip++) ptot[0]+=cpu.npart[ip-1]; // total of local particles *\/ */
    /* ntotsplit=ptot[0]; */
    /* MPI_Allreduce(MPI_IN_PLACE,&ntotsplit,1,MPI_LONG,MPI_SUM,cpu.comm); */
    /* printf("BEFORE proc %d receives %d particles %d stars freepart=%p ntot=%d\n",cpu.rank,deltan[0],deltan[1],cpu.freepart,ntotsplit); */
    mtot = multicheck(firstoct, ptot, param.lcoarse, param.lmax, cpu.rank, &cpu, &param, 10);
    setup_mpi(&cpu, firstoct, param.lmax, param.lcoarse, param.ngridmax, 1); // out of WMPI to compute the hash table
    MPI_Barrier(cpu.comm);
    cpu.firstoct = firstoct;
    mpi_exchange_part(&cpu, cpu.psendbuffer, cpu.precvbuffer, deltan, level);
    /* ptot[0]=0; */
    /* ptot[1]=0; */
    /* ptot[0]=deltan[0]; for(ip=1;ip<=param.lmax;ip++) ptot[0]+=cpu.npart[ip-1]; // total of local particles  */
    /* ntotsplit=ptot[0]; */
    /* MPI_Allreduce(MPI_IN_PLACE,&ntotsplit,1,MPI_LONG,MPI_SUM,cpu.comm); */
    printf("proc %d receives %d particles %d stars freepart=%p ntot=%d\n", cpu.rank, deltan[0], deltan[1], cpu.freepart, 262144 + deltan[0]);
    ptot[0] = 0;
    ptot[1] = 0;
    mtot = multicheck(firstoct, ptot, param.lcoarse, param.lmax, cpu.rank, &cpu, &param, 0);
#endif
#endif

    // ===== ED BOOKEEPING FOR SPLIT INITIAL CONDITIONS

    // Loop over time
    for(nsteps = nstepstart; (nsteps <= param.nsteps) * (tsim < tmax); nsteps++) {
        cpu.nsteps = nsteps;
#ifdef TESTCOSMO
        cosmo.aexp = interp_aexp(tsim, (double*)cosmo.tab_aexp, (double*)cosmo.tab_ttilde);
        cosmo.tsim = tsim;

        if(cpu.rank == RANK_DISP) printf("\n============== STEP %d aexp=%e z=%lf tconf=%e tmax=%e================\n", nsteps, cosmo.aexp, 1. / cosmo.aexp - 1., tsim, tmax);

#else //TESTCOSMO
#ifndef WRAD

        if(cpu.rank == RANK_DISP) printf("\n============== STEP %d tsim=%e ================\n", nsteps, tsim);

#else //WRAD

        if(cpu.rank == RANK_DISP) printf("\n============== STEP %d tsim=%e [%e Myr] ================\n", nsteps, tsim, tsim * param.unit.unit_t / MYR);

#endif //WRAD
#endif //TESTCOSMO

        // Resetting the timesteps

        for(level = 1; level <= levelmax; level++) {
            ndt[level - 1] = 0;
        }

        //Recursive Calls over levels
        double tg1 = 0, tg2 = 0, tg3 = 0, tg4 = 0;
#ifdef WMPI
        MPI_Barrier(cpu.comm);
        tg1 = MPI_Wtime();
#endif
        Advance_level(levelcoarse, adt, &cpu, &param, firstoct, lastoct, stencil, &gstencil, rstencil, ndt, nsteps, tsim);
#ifdef WMPI
        MPI_Barrier(cpu.comm);
        tg3 = MPI_Wtime();
#endif
#ifdef WRAD
#ifdef COARSERAD
        // inner loop on radiation
        REAL trad = 0.;
        REAL tsimrad = tsim;
        int nrad = 0.;

        if(cpu.rank == RANK_DISP) printf("START COARSE RAD with dt=%e\n", adt[levelcoarse - 1]);

        while(trad < adt[levelcoarse - 1]) {
            //if(cpu.rank==RANK_DISP) printf("step\n");
            double tcr1 = 0, tcr2 = 0;
#ifdef WMPI
            MPI_Barrier(cpu.comm);
            tcr1 = MPI_Wtime();
#endif
            Advance_level_RAD(levelcoarse, adt[levelcoarse - 1] - trad, adt_rad, &cpu, &param, firstoct, lastoct, stencil, &gstencil, rstencil, nsteps, tsimrad, nrad);
#ifdef WMPI
            MPI_Barrier(cpu.comm);
            tcr2 = MPI_Wtime();
#endif
            trad += adt_rad[levelcoarse - 1];
            tsimrad += adt_rad[levelcoarse - 1];
            nrad++;

            if(nrad % 10 == 0) if(cpu.rank == RANK_DISP) printf("rad iter=%d trad=%e tsimrad=%e tmax=%e done in %e secs\n", nrad, trad, tsimrad, adt[levelcoarse - 1], tcr2 - tcr1);
        }

#ifdef WMPI
        MPI_Barrier(cpu.comm);
        tg4 = MPI_Wtime();
#endif
#ifndef GPUAXL

        if(cpu.rank == RANK_DISP) printf("CPU : COARSE RAD DONE with %d steps in %e secs\n", nrad, tg4 - tg3);

#else

        if(cpu.rank == RANK_DISP) printf("GPU : COARSE RAD DONE with %d steps in %e secs\n", nrad, tg4 - tg3);

#endif // GPUAXL
#endif // COARSERAD
#endif // WRAD
#ifdef WMPI
        MPI_Barrier(cpu.comm);
        tg2 = MPI_Wtime();
#endif
#ifndef GPUAXL

        if(cpu.rank == RANK_DISP) printf("CPU GLOBAL TIME = %e t = %e\n", tg2 - tg1, tsim);

#else

        if(cpu.rank == RANK_DISP) printf("GPU GLOBAL TIME = %e t = %e\n", tg2 - tg1, tsim);

#endif
        // ==================================== dump
        cond1 = nsteps % param.ndumps == 0; // dump every ndump timestep
        cond2 = 0;                      // dump every dt_dump years
        cond3 = tsim + adt[levelcoarse - 1] >= tmax; // dump if simulation end
        cond4 = 0;                      // dump if aexp is in list_aexp
#ifdef TESTCOSMO

        if(param.aexpdump) {
            // dumpfile at specific outputs
            cond4 = cosmo.aexp > param.aexpdump;

            if(cond4) {
                if(fscanf(foutputs, "%e", &tempa) == EOF) {
                    param.aexpdump = 0;
                }

                else {
                    param.aexpdump = tempa;

                    if(cpu.rank == RANK_DISP) {
                        printf("next output aexp=%e\n", param.aexpdump);
                    }
                }
            }
        }

#endif

        if (param.dt_dump) {
            //cond1=0;
            int offset = 0;
#ifdef TESTCOSMO

            if (nsteps == 0) offset = (int)(param.cosmo->tphy / param.dt_dump);

            REAL a = param.cosmo->tphy;
            REAL b = (int)(ndumps + offset) * param.dt_dump;
            cond2 = a > b;

            if(cpu.rank == RANK_DISP)printf("t=%.2e yrs next dump at %.2e yrs\n", a, b + (a > b)*param.dt_dump);

#endif // TESTCOSMO
        }

        if(cond1 || cond2 || cond3 || cond4) {
            int fdump = FDUMP;

            if(cpu.nproc > fdump) {
                // dumping fields only
                int idump;

                for(idump = 0; idump < fdump; idump++) {
                    if(cpu.rank == RANK_DISP) printf("Dump batch # %d/%d\n", idump, fdump - 1);

                    if(cpu.rank % fdump == idump) dumpIO(tsim + adt[levelcoarse - 1], &param, &cpu, firstoct, adt, 0);

                    sleep(1);
#ifdef WMPI
                    MPI_Barrier(cpu.comm);
#endif
                }
            }

            else {
                dumpIO(tsim + adt[levelcoarse - 1], &param, &cpu, firstoct, adt, 0);
            }

            ndumps++;
        }

        dumpStepInfo(firstoct, &param, &cpu, nsteps, adt[levelcoarse - 1], (float)tsim);
#ifdef MOVIE
        dumpMovie(&param, &cpu, (float)tdump);
#endif // MOVIE
        //==================================== timestep completed, looping
        dt = adt[param.lcoarse - 1];
        tsim += dt;
    }

    ndumps -= 1;
    int fdump = FDUMP;

    if(cpu.nproc > fdump) {
        // dumping fields only
        int idump;

        for(idump = 0; idump < fdump; idump++) {
            if(cpu.rank == RANK_DISP && FDUMP > 1) printf("Dump batch # %d/%d\n", idump, fdump - 1);

            if(cpu.rank % fdump == idump) dumpIO(tsim, &param, &cpu, firstoct, adt, 1);

            sleep(1);
#ifdef WMPI
            MPI_Barrier(cpu.comm);
#endif
        }
    }

    else {
        dumpIO(tsim, &param, &cpu, firstoct, adt, 1);
    }

//printf("begin freeing\n");
///////////////////////////////////////////
// we are done let's free the ressources
///////////////////////////////////////////
    int debug_free = 0;

    if(cpu.rank == RANK_DISP) printf("FREE\n");

    if(debug_free) printf("grid\n");

    free(grid);

    if(debug_free) printf("firstoct\n");

    free(firstoct);

    if(debug_free) printf("lastoct\n");

    free(lastoct);

    if(debug_free) printf("octlist level\n");

    for(iLev = 0; iLev < levelcoarse; iLev++) {
        free(cpu.octList[iLev]);
    }

    for(iLev = levelcoarse; iLev < levelmax; iLev++) {
        free(cpu.octList[iLev]);
    }

    if(debug_free) printf("octlist main\n");

    free(cpu.octList);

    if(debug_free) printf("locNoct\n");

    free(cpu.locNoct);

    if(debug_free ) printf("part\n");

#ifdef PIC
    free(part);
#endif // PIC

    if(debug_free) printf("stencil\n");

#ifndef GPUAXL
    free(stencil);
#ifdef WRAD
    free(rstencil);
#endif // WRAD
    free(grav_stencil);
    free(gstencil.res);
    free(gstencil.pnew);
    free(gstencil.resLR);
#endif // GPUAXL

    if(debug_free) printf("dt\n");

#ifdef COARSERAD
    free(adt_rad);
#endif
    free(adt);
    free(ndt);
    free(ptot);

///////////////////////////////////////////
// free cpu
///////////////////////////////////////////

    if(debug_free) printf("cpu\n");

#if defined(MPIIO) || defined(HDF5)
    free(cpu.mpiio_ncells );
    free(cpu.mpiio_nparts );
#ifdef STARS
    free(cpu.mpiio_nstars );
#endif // STARS
#endif // MPIIO
    free(cpu.htable);
    free(cpu.noct);
    free(cpu.npart);
#ifdef STARS
    free(cpu.nstar);
#endif // STARS
    free(cpu.dict);
    free(cpu.mpinei);
    free(cpu.bndoct);
    free(cpu.allkmin);
    free(cpu.allkmax);
    free(cpu.nsend);
    free(cpu.nrecv);
    free(cpu.nsend_coarse);
    free(cpu.nrecv_coarse);
#ifdef WMPI

    for(i = 0; i < cpu.nnei; i++) {
        free(cpu.sendbuffer[i]);
        free(cpu.recvbuffer[i]);
    }

    free(cpu.sendbuffer);
    free(cpu.recvbuffer);
#ifdef PIC

    for(i = 0; i < cpu.nnei; i++) {
        free(cpu.psendbuffer[i]);
        free(cpu.precvbuffer[i]);
    }

    free(cpu.psendbuffer);
    free(cpu.precvbuffer);
#endif // PIC
#ifdef WHYDRO2

    for(i = 0; i < cpu.nnei; i++) {
        free(cpu.hsendbuffer[i]);
        free(cpu.hrecvbuffer[i]);
    }

    free(cpu.hsendbuffer);
    free(cpu.hrecvbuffer);
#endif // WHYDRO2
#ifdef WRAD

    for(i = 0; i < cpu.nnei; i++) {
        free(cpu.Rsendbuffer[i]);
        free(cpu.Rrecvbuffer[i]);
    }

    free(cpu.Rsendbuffer);
    free(cpu.Rrecvbuffer);
#endif // WRAD
#endif // WMPI

///////////////////////////////////////////
// free param
///////////////////////////////////////////
    if(debug_free) printf("param\n");

    free(param.atomic.space_bound);
    free(param.atomic.time_bound);
    free(param.atomic.hnu);
    free(param.atomic.alphae);
    free(param.atomic.alphai);
    free(param.atomic.factgrp);
#ifdef SUPERNOVAE
    //free(param.sn->mass_loss_t);
    //free(param.sn->mass_loss_mass);
    //free(param.sn->egy_loss_t);
    //free(param.sn->egy_loss_egy);
#endif // SUPERNOVAE
#if defined(UVBKG) || defined(STARS_TO_UVBKG)
    free(param.uv.redshift);
    free(param.uv.Nphot);
    free(param.uv.value);
#endif // defined
#ifdef MOVIE
    free(param.movie->map);
    free(param.movie->map_reduce);
#endif // MOVIE

///////////////////////////////////////////
// free GPU
///////////////////////////////////////////
    if(debug_free) printf("gpu\n");

#ifdef GPUAXL
#ifdef WGRAV
    //destroy_pinned_gravstencil(&gstencil,gstride);
    //destroy_gravstencil_GPU(&cpu,gstride);
#endif
#ifdef WHYDRO2
    //destroy_pinned_stencil(&stencil,hstride);
    destroy_hydstencil_GPU(&cpu, hstride);
#endif
#ifdef WRAD
    //destroy_pinned_stencil_rad(&rstencil,rstride);
    destroy_radstencil_GPU(&cpu, rstride);
#endif
#endif // GPUAXL
///////////////////////////////////////////
///////////////////////////////////////////
///////////////////////////////////////////
    REAL tend = MPI_Wtime();

    if(cpu.rank == RANK_DISP) {
        //fclose(param.fpegy);
        printf("Done ..... in %.2e CPU hours\n", (tend - tstart) / 3600 * cpu.nproc);
    }

#ifdef WMPI
    MPI_Barrier(cpu.comm);
    MPI_Finalize();
#endif
    return 0;
}

