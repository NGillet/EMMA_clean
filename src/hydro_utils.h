int hydroS(struct HGRID* stencil, int level, int curcpu, int nread, int stride, REAL dx, REAL dt);
int hydroM(struct HGRID* stencil, int level, int curcpu, int nread, int stride, REAL dx, REAL dt);
REAL comptstep_hydro(int levelcoarse, int levelmax, struct OCT** firstoct, REAL fa, REAL fa2, struct CPUINFO* cpu, REAL tmax);
REAL comptstep_ff(int levelcoarse, int levelmax, struct OCT** firstoct, REAL aexp, struct CPUINFO* cpu, REAL tmax);
void correct_grav_hydro(struct OCT* octstart, struct CPUINFO* cpu, REAL dt);
REAL comptstep_force(int levelcoarse, int levelmax, struct OCT** firstoct, REAL aexp, struct CPUINFO* cpu, REAL tmax);
void grav_correction(int level, struct RUNPARAMS* param, struct OCT** firstoct, struct CPUINFO* cpu, REAL dt);
void hydro(int level, struct RUNPARAMS* param, struct OCT** firstoct,  struct CPUINFO* cpu, struct HGRID* stencil, int stride, REAL dtnew);
void HydroSolver(int level, struct RUNPARAMS* param, struct OCT** firstoct,  struct CPUINFO* cpu, struct HGRID* stencil, int stride, REAL dtnew);
void clean_new_hydro(int level, struct RUNPARAMS* param, struct OCT** firstoct, struct CPUINFO* cpu);
void coarse2fine_hydrolin(struct CELL* cell, struct Wtype* Wi);
void coarse2fine_hydro(struct CELL* cell, struct Wtype* Wi);
void coarse2fine_hydro2(struct CELL* cell, struct Wtype* Wi);
void getE(struct Wtype* W);
void W2U(struct Wtype* W, struct Utype* U);
void U2W(struct Utype* U, struct Wtype* W);
void printWtype(struct Wtype* W);
