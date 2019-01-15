/*! \file swiftinterface.cxx
 *  \brief this file contains routines that allow the velociraptor library to interface with the swift N-body code from within swift.
 */

#include "swiftinterface.h"

#ifdef SWIFTINTERFACE

Options libvelociraptorOpt;
//KDTree *mpimeshtree;
//Particle *mpimeshinfo;

/// \defgroup SWIFTCONFIGERRORS Errors for swift
//@{
///
#define SWIFTCONFIGOPTMISSING 8
#define SWIFTCONFIGOPTERROR 9
#define SWIFTCONFIGOPTCONFLICT 10
//@}

///check configuration options with swift
inline int ConfigCheckSwift(Options &opt, Swift::siminfo &s)
{
#ifndef USEMPI
    int ThisTask=0;
#endif
    if (opt.iBaryonSearch && !(opt.partsearchtype==PSTALL || opt.partsearchtype==PSTDARK)) {
        if (ThisTask==0) cerr<<"Conflict in config file: both gas/star/etc particle type search AND the separate baryonic (gas,star,etc) search flag are on. Check config\n";
        return SWIFTCONFIGOPTCONFLICT;
    }
    if (opt.iBoundHalos && opt.iKeepFOF) {
        if (ThisTask==0) cerr<<"Conflict in config file: Asking for Bound Field objects but also asking to keep the 3DFOF/then run 6DFOF. This is incompatible. Check config\n";
        return SWIFTCONFIGOPTCONFLICT;
    }
    if (opt.HaloMinSize==-1) opt.HaloMinSize=opt.MinSize;

    if (s.idarkmatter == false && opt.partsearchtype==PSTDARK) {
        if (ThisTask==0) cerr<<"Conflict in config file: simulation contains no dark matter but searching only dark matter. This is incompatible. Check config\n";
        return SWIFTCONFIGOPTCONFLICT;
    }
    if (s.idarkmatter == false && opt.partsearchtype==PSTALL && opt.iBaryonSearch) {
        if (ThisTask==0) cerr<<"Conflict in config file: simulation contains no dark matter but using dark matter to define links when searching all particles . This is incompatible. Check config\n";
        return SWIFTCONFIGOPTCONFLICT;
    }
    if (s.igas == false && opt.partsearchtype==PSTGAS) {
        if (ThisTask==0) cerr<<"Conflict in config file: simulation contains no gas but searching only gas. This is incompatible. Check config\n";
        return SWIFTCONFIGOPTCONFLICT;
    }
    if ((s.istar == false && opt.partsearchtype==PSTSTAR)) {
        if (ThisTask==0) cerr<<"Conflict in config file: simulation contains no gas but searching only stars or using stars as basis for links. This is incompatible. Check config\n";
        return SWIFTCONFIGOPTCONFLICT;
    }

#ifndef USEHDF
    if (opt.ibinaryout==OUTHDF){
        if (ThisTask==0) cerr<<"Code not compiled with HDF output enabled. Recompile with this enabled or change Binary_output.\n";
        return SWIFTCONFIGOPTERROR;
    }
#endif

#ifndef USEADIOS
    if (opt.ibinaryout==OUTADIOS){
        if (ThisTask==0) cerr<<"Code not compiled with ADIOS output enabled. Recompile with this enabled or change Binary_output.\n";
        return SWIFTCONFIGOPTERROR;
    }
#endif

    if (ThisTask==0) {
    cout<<"CONFIG INFO SUMMARY -------------------------- "<<endl;
    switch(opt.partsearchtype)
    {
            case PSTALL:
                cout<<"Searching all particles regardless of type"<<endl;
                break;
            case PSTDARK:
                cout<<"Searching dark matter particles "<<endl;
                break;
            case PSTGAS:
                cout<<"Searching gas particles "<<endl;
                break;
            case PSTSTAR:
                cout<<"Searching star particles"<<endl;
                break;
            case PSTBH:
                cout<<"Searching BH particles?! Really? Are there enough?"<<endl;
                break;
    }
    if (opt.fofbgtype==FOF6D) cout<<"Field objects found with 3d FOF"<<endl;
    else if (opt.fofbgtype==FOF6D) cout<<"Field objects found with initial 3d FOF then use single dispersion to find 6d FOFs"<<endl;
    else if (opt.fofbgtype==FOF6DADAPTIVE) cout<<"Field objects found with initial 3d FOF then use adaptive dispersion to find 6d FOFs"<<endl;
    else if (opt.fofbgtype==FOFSTNOSUBSET) cout<<"Field objects found with initial 3d FOF then use phase-space stream finding algorithm"<<endl;
    if (opt.fofbgtype<=FOF6D && opt.iKeepFOF) cout<<"Field objects found with initial 3d FOF then use dispersion to find 6d FOFs and 3dFOF objects kept as base objects (consider them inter halo stellar mass or ICL for stellar only searches) "<<endl;
    if (opt.iBaryonSearch==1) cout<<"Baryons treated separately for substructure search"<<endl;
    else if (opt.iBaryonSearch==2) cout<<"Baryons treated separately for substructure search and also FOF field search"<<endl;
    if (opt.iSingleHalo) cout<<"Field objects NOT searched for, assuming single Halo and subsearch using mean field first step"<<endl;
    cout<<"Allowed potential to kinetic ratio when unbinding particles "<<opt.uinfo.Eratio<<endl;
    if (opt.HaloMinSize!=opt.MinSize) cout<<"Field objects (aka Halos) have different minimum required size than substructures: "<<opt.HaloMinSize<<" vs "<<opt.MinSize<<endl;
    cout<<"Units: L="<<opt.L<<", M="<<opt.M<<", V="<<opt.V<<", G="<<opt.G<<endl;
    if (opt.ibinaryout) cout<<"Binary output"<<endl;
    if (opt.iseparatefiles) cout<<"Separate files output"<<endl;
    if (opt.iextendedoutput) cout<<"Extended output for particle extraction from input files"<<endl;
    if (opt.iHaloCoreSearch) cout<<"Searching for 6dfof cores so as to disentangle mergers"<<endl;
    if (opt.iHaloCoreSearch && opt.iAdaptiveCoreLinking) cout<<"With adaptive linking lengths"<<endl;
    if (opt.iHaloCoreSearch && opt.iPhaseCoreGrowth) cout<<"With with phase-space tensor core assignment"<<endl;
    if (opt.inputtype==IOGADGET) cout<<"Gadget file particle input "<<endl;
    else if (opt.inputtype==IOTIPSY) cout<<"Tipsy file particle input "<<endl;
    else if (opt.inputtype==IORAMSES) cout<<"RAMSES file particle input "<<endl;
#ifdef USEHDF
    else if (opt.inputtype==IOHDF) cout<<"HDF file particle input "<<endl;
#endif
#ifdef USEXDR
    else if (opt.inputtype==IONCHILADA) cout<<"NCHILADA file particle input "<<endl;
#endif
    cout<<" -------------------------- "<<endl;
    }
    return 1;
}


int InitVelociraptor(char* configname, unitinfo u, siminfo s, const int numthreads)
{
    cout<<"Initialising VELOCIraptor..."<< endl;
    // if mpi invokved, init the velociraptor tasks and openmp threads
#ifdef USEMPI
    //find out how big the SPMD world is
    MPI_Comm_size(MPI_COMM_WORLD,&NProcs);
    //mpi_domain=new MPI_Domain[NProcs];
    mpi_nlocal=new Int_t[NProcs];
    mpi_nsend=new Int_t[NProcs*NProcs];
    mpi_ngroups=new Int_t[NProcs];
    //and this processes' rank is
    MPI_Comm_rank(MPI_COMM_WORLD,&ThisTask);
    //store MinSize as when using mpi prior to stitching use min of 2;
    MinNumMPI=2;
#endif
#ifdef USEOPENMP
    omp_set_num_threads(numthreads);
#endif
    int iconfigflag;
    ///read the parameter file
    libvelociraptorOpt.pname = configname;
    cout<<"Reading VELOCIraptor config file..."<< endl;
    GetParamFile(libvelociraptorOpt);
    ///check configuration
    iconfigflag = ConfigCheckSwift(libvelociraptorOpt, s);
    if (iconfigflag != 1) return iconfigflag;

    cout<<"Setting cosmology, units, sim stuff "<<endl;
    ///set units, here idea is to convert internal units so that have kpc, km/s, solar mass
    libvelociraptorOpt.lengthtokpc=1.0;
    libvelociraptorOpt.velocitytokms=1.0;
    libvelociraptorOpt.masstosolarmass=1.0;
    libvelociraptorOpt.L=u.lengthtokpc;
    libvelociraptorOpt.M=u.masstosolarmass;
    libvelociraptorOpt.V=u.velocitytokms;

    //set cosmological parameters that do not change
    ///these should be in units of kpc, km/s, and solar mass
    libvelociraptorOpt.G=u.gravity;
    libvelociraptorOpt.U=u.energyperunitmass;
    libvelociraptorOpt.H=u.hubbleunit;

    //write velociraptor configuration info, appending .configuration to the input config file and writing every config option
    libvelociraptorOpt.outname = configname;
    WriteVELOCIraptorConfig(libvelociraptorOpt);

    cout<<"Finished initialising VELOCIraptor"<<endl;

    //return the configuration flag value
    return iconfigflag;

}

void SetVelociraptorSimulationState(cosmoinfo c, siminfo s)
{
    ///set cosmology
    libvelociraptorOpt.a=c.atime;
    libvelociraptorOpt.h=c.littleh;
    libvelociraptorOpt.Omega_m=c.Omega_m;
    libvelociraptorOpt.Omega_b=c.Omega_b;
    libvelociraptorOpt.Omega_cdm=c.Omega_cdm;
    libvelociraptorOpt.Omega_Lambda=c.Omega_Lambda;
    libvelociraptorOpt.w_de=c.w_de;

    //set some sim information
    libvelociraptorOpt.p=s.period;
    libvelociraptorOpt.zoomlowmassdm=s.zoomhigresolutionmass;
    libvelociraptorOpt.icosmologicalin=s.icosmologicalsim;
    libvelociraptorOpt.ellxscale=s.interparticlespacing;
    libvelociraptorOpt.uinfo.eps*=libvelociraptorOpt.ellxscale;

    if (libvelociraptorOpt.icosmologicalin) {
        libvelociraptorOpt.ellxscale*=libvelociraptorOpt.a;
        libvelociraptorOpt.uinfo.eps*=libvelociraptorOpt.a;
        double Hubble=libvelociraptorOpt.h*libvelociraptorOpt.H*sqrt((1-libvelociraptorOpt.Omega_m-libvelociraptorOpt.Omega_Lambda)*pow(libvelociraptorOpt.a,-2.0)+libvelociraptorOpt.Omega_m*pow(libvelociraptorOpt.a,-3.0)+libvelociraptorOpt.Omega_Lambda);
        libvelociraptorOpt.rhobg=3.*Hubble*Hubble/(8.0*M_PI*libvelociraptorOpt.G)*libvelociraptorOpt.Omega_m;
        //if libvelociraptorOpt.virlevel<0, then use virial overdensity based on Bryan and Norman 1998 virialization level is given by
        if (libvelociraptorOpt.virlevel<0)
        {
            Double_t bnx=-((1-libvelociraptorOpt.Omega_m-libvelociraptorOpt.Omega_Lambda)*pow(libvelociraptorOpt.a,-2.0)+libvelociraptorOpt.Omega_Lambda)/((1-libvelociraptorOpt.Omega_m-libvelociraptorOpt.Omega_Lambda)*pow(libvelociraptorOpt.a,-2.0)+libvelociraptorOpt.Omega_m*pow(libvelociraptorOpt.a,-3.0)+libvelociraptorOpt.Omega_Lambda);
            libvelociraptorOpt.virlevel=(18.0*M_PI*M_PI+82.0*bnx-39*bnx*bnx)/libvelociraptorOpt.Omega_m;
        }
    }
    else {
        libvelociraptorOpt.rhobg=1.0;
    }

    //assume above is in comoving is cosmological simulation and then need to correct to physical
    if (libvelociraptorOpt.icosmologicalin) {
        libvelociraptorOpt.p*=libvelociraptorOpt.a;
        libvelociraptorOpt.ellxscale*=libvelociraptorOpt.a;
        libvelociraptorOpt.uinfo.eps*=libvelociraptorOpt.a;
    }
    libvelociraptorOpt.uinfo.icalculatepotential=true;

    // Set mesh information. (has this been mapped to physical?)
    libvelociraptorOpt.spacedimension[0] = s.spacedimension[0];
    libvelociraptorOpt.spacedimension[1] = s.spacedimension[1];
    libvelociraptorOpt.spacedimension[2] = s.spacedimension[2];
    libvelociraptorOpt.cellwidth[0] = s.cellwidth[0];
    libvelociraptorOpt.cellwidth[1] = s.cellwidth[1];
    libvelociraptorOpt.cellwidth[2] = s.cellwidth[2];
    libvelociraptorOpt.icellwidth[0] = s.icellwidth[0];
    libvelociraptorOpt.icellwidth[1] = s.icellwidth[1];
    libvelociraptorOpt.icellwidth[2] = s.icellwidth[2];
    libvelociraptorOpt.numcells = s.numcells;
    libvelociraptorOpt.numcellsperdim = cbrt(s.numcells);
    libvelociraptorOpt.cellloc = s.cellloc;

#ifdef USEMPI
    //if single halo, use minsize to initialize the old minimum number
    //else use the halominsize since if mpi and not single halo, halos localized to mpi domain for substructure search
    if (libvelociraptorOpt.iSingleHalo) MinNumOld=libvelociraptorOpt.MinSize;
    else MinNumOld=libvelociraptorOpt.HaloMinSize;
    mpi_period = libvelociraptorOpt.p;
#endif
}

int InvokeVelociraptor(const int snapnum, char* outputname,
    cosmoinfo c, siminfo s,
    const size_t num_gravity_parts, const size_t num_hydro_parts,
    struct swift_vel_part *swift_parts, const int *cell_node_ids,
    const int numthreads)
{
#ifndef GASON
    cout<<"Gas has not been turned on in VELOCIraptor. Set GASON in Makefile.config and recompile VELOCIraptor."<<endl;
    return 0;
#endif
#ifdef USEMPI
    if (ThisTask==0) cout<<"VELOCIraptor/STF running with MPI. Number of mpi threads: "<<NProcs<<endl;
#else
    int ThisTask=0;
    int NProcs=1;
    Int_t Nlocal, Ntotal, Nmemlocal;
    Int_t Nmemlocalbaryon, Nlocalbaryon[1];
#endif
    int nthreads;
#ifdef USEOPENMP
#pragma omp parallel
{
    if (omp_get_thread_num()==0) nthreads=omp_get_num_threads();
}
    if (ThisTask==0) cout<<"VELOCIraptor/STF running with OpenMP. Number of openmp threads: "<<nthreads<<endl;
#else
    nthreads=1;
#endif

    libvelociraptorOpt.outname = outputname;
    libvelociraptorOpt.snapshotvalue = HALOIDSNVAL* snapnum;

    //write associated units and simulation details (which contains scale factor/time information)
    SetVelociraptorSimulationState(c, s);
    WriteSimulationInfo(libvelociraptorOpt);
    WriteUnitInfo(libvelociraptorOpt);

    vector<Particle> parts;
    Particle *pbaryons;
    Int_t *pfof, *pfofall, *pfofbaryons, *numingroup,**pglist;
    Int_t nbaryons, ndark;
    Int_t ngroup, nhalos;
    //KDTree *tree;
    //to store information about the group
    PropData *pdata=NULL,*pdatahalos=NULL;
    double time1;

    /// Set pointer to cell node IDs
    libvelociraptorOpt.cellnodeids = cell_node_ids;

    Nlocal=Nmemlocal=num_gravity_parts;
    Nmemlocal*=(1+libvelociraptorOpt.mpipartfac); /* JSW: Not set in parameter file. */
#ifdef USEMPI
    MPI_Allreduce(&Nlocal, &Ntotal, 1, MPI_Int_t, MPI_SUM, MPI_COMM_WORLD);
    MPI_Allgather(&Nlocal, 1, MPI_Int_t, mpi_nlocal, 1, MPI_Int_t, MPI_COMM_WORLD);
#else
    Ntotal=Nlocal;
#endif
    parts.resize(Nmemlocal);

    cout<<"Copying particle data..."<< endl;
    time1=MyGetTime();

    ndark = num_gravity_parts - num_hydro_parts, nbaryons = num_hydro_parts;
    Nlocalbaryon[0]=nbaryons;
    Nmemlocalbaryon=Nlocalbaryon[0];

    /// If we are performing a baryon search, sort the particles so that the DM particles are at the start of the array followed by the gas particles.
    if (libvelociraptorOpt.iBaryonSearch>0 && libvelociraptorOpt.partsearchtype!=PSTALL) {

      size_t dmOffset = 0, gasOffset = 0;

      pbaryons=&(parts.data()[ndark]);

      cout<<"There are "<<nbaryons<<" gas particles and "<<ndark<<" DM particles."<<endl;
      for(auto i=0; i<Nlocal; i++) {
        if(swift_parts[i].type == DARKTYPE) {
          parts[dmOffset++] = Particle(swift_parts[i]);
        }
        else {
          if(swift_parts[i].type == GASTYPE) {
            pbaryons[gasOffset++] = Particle(swift_parts[i]);
          }
          else if(swift_parts[i].type == STARTYPE) {
            cout<<"Star particle type not supported yet. Exiting..."<<endl;
            return 0;
          }
          else if(swift_parts[i].type == BHTYPE) {
            cout<<"Black hole particle type not supported yet. Exiting..."<<endl;
            return 0;
          }
          else {
            cout<<"Unknown particle type found: index="<<i<<" type="<<swift_parts[i].type<<" while treating baryons differently. Exiting..."<<endl;
            return 0;
          }
        }
      }
    }
    else {
      for(auto i=0; i<Nlocal; i++) {
        parts[i] = Particle(swift_parts[i]);
        if(swift_parts[i].type == STARTYPE) {
          cout<<"Star particle type not supported yet. Exiting..."<<endl;
          return 0;
        }
        else if(swift_parts[i].type == BHTYPE) {
          cout<<"Black hole particle type not supported yet. Exiting..."<<endl;
          return 0;
        }
        else if(swift_parts[i].type != DARKTYPE && swift_parts[i].type != GASTYPE) {
          cout<<"Unknown particle type found: index="<<i<<" type="<<swift_parts[i].type<<" when loading particles. Exiting..."<<endl;
          return 0;
        }
     }

/*
      for(auto i=0; i<Nlocal; i++) {
        parts[i] = Particle(swift_parts[i]);
        parts[i].SetType(DARKTYPE);
     }
*/

    }

    time1=MyGetTime()-time1;
    cout<<"Finished copying particle data."<< endl;
    cout<<"TIME::"<<ThisTask<<" took "<<time1<<" to copy "<<Nlocal<<" particles from SWIFT to a local format. Out of "<<Ntotal<<endl;
    cout<<ThisTask<<" There are "<<Nlocal<<" particles and have allocated enough memory for "<<Nmemlocal<<" requiring "<<Nmemlocal*sizeof(Particle)/1024./1024./1024.<<"GB of memory "<<endl;
    if (libvelociraptorOpt.iBaryonSearch>0) cout<<ThisTask<<"There are "<<Nlocalbaryon[0]<<" baryon particles and have allocated enough memory for "<<Nmemlocalbaryon<<" requiring "<<Nmemlocalbaryon*sizeof(Particle)/1024./1024./1024.<<"GB of memory "<<endl;
    cout<<ThisTask<<" will also require additional memory for FOF algorithms and substructure search. Largest mem needed for preliminary FOF search. Rough estimate is "<<Nlocal*(sizeof(Int_tree_t)*8)/1024./1024./1024.<<"GB of memory"<<endl;

    //build binary tree to store cells for quick search
    /*
    Double_t *period=NULL;
    if (libvelociraptorOpt.p>0) {
        period=new Double_t[3];
        for (int j=0;j<3;j++) period[j]=libvelociraptorOpt.p;
    }
    for (auto i=0;i<libvelociraptorOpt.numcells; i++) for (auto j=0;j<3;j++) mpimeshinfo[i].SetPosition(j,libvelociraptorOpt.cellloc[i].loc[j]);
    mpimeshtree=new KDTree(mpimeshinfo,libvelociraptorOpt.numcells,1,mpimeshtree->TPHYS,mpimeshtree->KEPAN,100,0,0,0,period);
    */

    //
    // Perform FOF search.
    //
    time1=MyGetTime();
    pfof=SearchFullSet(libvelociraptorOpt,Nlocal,parts,ngroup);
    time1=MyGetTime()-time1;
    cout<<"TIME::"<<ThisTask<<" took "<<time1<<" to search "<<Nlocal<<" with "<<nthreads<<endl;
    nhalos=ngroup;
    //if caculating inclusive halo masses, then for simplicity, I assume halo id order NOT rearranged!
    //this is not necessarily true if baryons are searched for separately.
    if (libvelociraptorOpt.iInclusiveHalo) {
        pdatahalos=new PropData[nhalos+1];
        Int_t *numinhalos=BuildNumInGroup(Nlocal, nhalos, pfof);
        Int_t *sortvalhalos=new Int_t[Nlocal];
        Int_t *originalID=new Int_t[Nlocal];
        for (Int_t i=0;i<Nlocal;i++) {sortvalhalos[i]=pfof[i]*(pfof[i]>0)+Nlocal*(pfof[i]==0);originalID[i]=parts[i].GetID();parts[i].SetID(i);}
        Int_t *noffsethalos=BuildNoffset(Nlocal, parts.data(), nhalos, numinhalos, sortvalhalos);
        GetInclusiveMasses(libvelociraptorOpt, Nlocal, parts.data(), nhalos, pfof, numinhalos, pdatahalos, noffsethalos);
        qsort(parts.data(),Nlocal,sizeof(Particle),IDCompare);
        delete[] numinhalos;
        delete[] sortvalhalos;
        delete[] noffsethalos;
        for (Int_t i=0;i<Nlocal;i++) parts[i].SetID(originalID[i]);
        delete[] originalID;
    }

    //
    // Substructure search.
    //
    if (libvelociraptorOpt.iSubSearch) {
        cout<<"Searching subset"<<endl;
        time1=MyGetTime();
        //if groups have been found (and localized to single MPI thread) then proceed to search for subsubstructures
        SearchSubSub(libvelociraptorOpt, Nlocal, parts, pfof,ngroup,nhalos,pdatahalos);
        time1=MyGetTime()-time1;
        cout<<"TIME::"<<ThisTask<<" took "<<time1<<" to search for substructures "<<Nlocal<<" with "<<nthreads<<endl;
    }
    pdata=new PropData[ngroup+1];
    //if inclusive halo mass required
    if (libvelociraptorOpt.iInclusiveHalo && ngroup>0) {
        CopyMasses(libvelociraptorOpt,nhalos,pdatahalos,pdata);
        delete[] pdatahalos;
    }

    //
    // Search for baryons
    //

    //if only searching initially for dark matter groups, once found, search for associated baryonic structures if requried
    if (libvelociraptorOpt.iBaryonSearch>0) {
        time1=MyGetTime();
        if (libvelociraptorOpt.partsearchtype==PSTDARK) {
            pfofall=SearchBaryons(libvelociraptorOpt, nbaryons, pbaryons, Nlocal, parts, pfof, ngroup,nhalos,libvelociraptorOpt.iseparatefiles,libvelociraptorOpt.iInclusiveHalo,pdata);
            pfofbaryons=&pfofall[Nlocal];
        }
        //if FOF search overall particle types then running sub search over just dm and need to associate baryons to just dm particles must determine number of baryons, sort list, run search, etc
        //but only need to run search if substructure has been searched
        else if (libvelociraptorOpt.iSubSearch==1){
            nbaryons=0;
            ndark=0;
            for (Int_t i=0;i<Nlocal;i++) {
                if (parts[i].GetType()==DARKTYPE)ndark++;
                else nbaryons++;
            }
            pbaryons=NULL;
            SearchBaryons(libvelociraptorOpt, nbaryons, pbaryons, ndark, parts, pfof, ngroup,nhalos,libvelociraptorOpt.iseparatefiles,libvelociraptorOpt.iInclusiveHalo,pdata);
        }
        time1=MyGetTime()-time1;
        cout<<"TIME::"<<ThisTask<<" took "<<time1<<" to search baryons  with "<<nthreads<<endl;
    }

    //get mpi local hierarchy
    Int_t *nsub,*parentgid, *uparentgid,*stype;
    nsub=new Int_t[ngroup+1];
    parentgid=new Int_t[ngroup+1];
    uparentgid=new Int_t[ngroup+1];
    stype=new Int_t[ngroup+1];
    Int_t nhierarchy=GetHierarchy(libvelociraptorOpt,ngroup,nsub,parentgid,uparentgid,stype);
    CopyHierarchy(libvelociraptorOpt,pdata,ngroup,nsub,parentgid,uparentgid,stype);

    //calculate data and output
    numingroup=BuildNumInGroup(Nlocal, ngroup, pfof);
    pglist=SortAccordingtoBindingEnergy(libvelociraptorOpt,Nlocal,parts.data(),ngroup,pfof,numingroup,pdata);//alters pglist so most bound particles first
    WriteProperties(libvelociraptorOpt,ngroup,pdata);
    WriteGroupCatalog(libvelociraptorOpt, ngroup, numingroup, pglist, parts);
    //if baryons have been searched output related gas baryon catalogue
    if (libvelociraptorOpt.iBaryonSearch>0 || libvelociraptorOpt.partsearchtype==PSTALL){
      WriteGroupPartType(libvelociraptorOpt, ngroup, numingroup, pglist, parts);
    }

    for (Int_t i=1;i<=ngroup;i++) delete[] pglist[i];
    delete[] pglist;
    //delete[] parts;

    //delete tree used to search mpi mesh
    //delete mpimeshtree;

    ///\todo need to return fof and substructure information back to swift

    cout<<"VELOCIraptor returning."<< endl;

    return 1;
}

#endif