static const char help[] =
"Build and view a tiny three-triangle mesh using DMPlex, and integrate a\n"
"scalar function over it.  Option prefixes tny_ and plex_view_.\n\n";

/* either build DMPlex via call to DMPlexCreateFromCellList()
    ./tiny
or by directly setting cones "by hand":
    ./tiny -tny_by_hand

compare these views:
    ./tiny -dm_view
    ./tiny -section_view
    ./tiny -v_vec_view  FIXME ?

homemade DMPlex mesh views:  FIXME put back in plexview.h|c
    ./tiny -plex_view_ranges
    ./tiny -plex_view_ranges -plex_view_use_height
    ./tiny -plex_view_cell_cones
    ./tiny -plex_view_vertex_supports
    ./tiny -plex_view_coords

check it out: parallel refinement already works!:
    mpiexec -n 2 ./tiny -tny_ranges -dm_refine 1 -tny_coords_view

FIXME add option -tny_element P1|P2|P3
*/

#include <petsc.h>

// Describe the mesh "triangle style" with separate numbering for cells and vertices.
static const int    dim = 2,
                    ncell = 3,
                    nvert = 5,
                    cells[9] = {0, 3, 2,  // 9 = ncell * (dim+1)
                                0, 2, 1,
                                2, 3, 4};
static const double coordverts[10] = {0.0, 0.0,  // 10 = nvert * dim
                                      0.0, 1.0,
                                      0.5, 1.0,
                                      1.0, 0.0,
                                      1.0, 1.0};

// Describe same mesh, but directly as DMPlex, i.e. by giving cell and
// edge cones in DAG.  These values are generated by DMPlexCreateFromCellList()
// internally, so they are redundant if we use that create method.
static const int npoint = 15,
                 ccone[3][3] = {{8,9,10},
                                {10,11,12},
                                {9,13,14}},
                 econe[7][2] = {{3,6},
                                {5,6},
                                {3,5},
                                {4,5},
                                {3,4},
                                {6,7},
                                {5,7}};

extern PetscErrorCode CreateMeshByHand(DM*);
extern PetscErrorCode CreateCoordinateSectionByHand(DM*);
extern PetscErrorCode PlexViewFromOptions(DM);
extern PetscErrorCode PlexViewRanges(DM, PetscBool);
extern PetscErrorCode PlexViewFans(DM, int, int, int);
extern PetscErrorCode VecViewLocalStdout(Vec,MPI_Comm);

int main(int argc,char **argv) {
    PetscErrorCode ierr;
    DM            dmplex;
    PetscSection  section;
    PetscBool     by_hand = PETSC_FALSE;

    PetscInitialize(&argc,&argv,NULL,help);

    ierr = PetscOptionsBegin(PETSC_COMM_WORLD, "tny_", "options for tiny", "");CHKERRQ(ierr);
    ierr = PetscOptionsBool("-by_hand", "use by-hand construction",
                            "tiny.c", by_hand, &by_hand, NULL);CHKERRQ(ierr);
    ierr = PetscOptionsEnd();

    if (by_hand) {
        ierr = CreateMeshByHand(&dmplex); CHKERRQ(ierr);
        ierr = CreateCoordinateSectionByHand(&dmplex); CHKERRQ(ierr);
    } else {
        PetscMPIInt rank;
        ierr = MPI_Comm_rank(PETSC_COMM_WORLD, &rank); CHKERRQ(ierr);
        if (rank == 0) { // create mesh on rank 0
            ierr = DMPlexCreateFromCellList(PETSC_COMM_WORLD,
                dim,ncell,nvert,dim+1,
                PETSC_TRUE,  // "interpolate" flag; TRUE means "topologically-interpolate"
                             // i.e. create edges (1D) from vertices (0D) and cells (2D)
                cells,dim,coordverts,
                &dmplex); CHKERRQ(ierr);
        } else { // empty mesh on rank > 0
            ierr = DMPlexCreateFromCellList(PETSC_COMM_WORLD,
                dim,0,0,dim+1,PETSC_TRUE,NULL,dim,NULL,&dmplex); CHKERRQ(ierr);
        }
    }

    // distribute mesh over processes using default partitioner
    {
        DM  distributedMesh = NULL;
        // overlap of 0 is appropriate to P2 etc. FEM:
        ierr = DMPlexDistribute(dmplex, 0, NULL, &distributedMesh);CHKERRQ(ierr);
        if (distributedMesh) {
          ierr = DMDestroy(&dmplex);CHKERRQ(ierr);
          dmplex  = distributedMesh;
        }
    }

    // reset names before viewing
    ierr = PetscObjectSetName((PetscObject)dmplex, "tiny mesh"); CHKERRQ(ierr);
    {
        DM           coorddm;
        PetscSection coordsection;
        ierr = DMGetCoordinateDM(dmplex, &coorddm); CHKERRQ(ierr);
        ierr = DMGetDefaultSection(coorddm, &coordsection); CHKERRQ(ierr);
        ierr = PetscObjectSetName((PetscObject)coordsection, "coordinate section"); CHKERRQ(ierr);
    }

    ierr = DMSetFromOptions(dmplex); CHKERRQ(ierr);
    ierr = PlexViewFromOptions(dmplex); CHKERRQ(ierr);

    // Create nodes (degrees of freedom) for P2 elements using PetscSection.
    // Have 1 dof on each vertex (depth==0) and 1 dof on each edge (depth==1).
    {
        int  j, vertexstart, edgeend;
        ierr = DMPlexGetDepthStratum(dmplex, 0, &vertexstart, NULL); CHKERRQ(ierr);
        ierr = DMPlexGetDepthStratum(dmplex, 1, NULL, &edgeend); CHKERRQ(ierr);
        ierr = PetscSectionCreate(PETSC_COMM_WORLD,&section); CHKERRQ(ierr);
        ierr = PetscObjectSetName((PetscObject)section, "P2 scalar field section"); CHKERRQ(ierr);
        ierr = PetscSectionSetNumFields(section, 1); CHKERRQ(ierr);
        ierr = PetscSectionSetChart(section, vertexstart, edgeend); CHKERRQ(ierr);
        for (j = vertexstart; j < edgeend; ++j) {
            ierr = PetscSectionSetDof(section, j, 1); CHKERRQ(ierr);
            ierr = PetscSectionSetFieldDof(section, j, 0, 1); CHKERRQ(ierr);
        }
        ierr = PetscSectionSetUp(section); CHKERRQ(ierr);
        ierr = DMSetDefaultSection(dmplex, section); CHKERRQ(ierr);
        ierr = PetscObjectViewFromOptions((PetscObject)section,NULL,"-section_view"); CHKERRQ(ierr);
    }

#if 0
//TO DO THE FOLLOWING, NAMELY SET VALUES ON ONE CELL, NEED TO CHECK IF WE OWN THAT CELL
    // assign values in a global Vec for the section, i.e. on P2 dofs
    // FIXME a more interesting task would be to have an f(x,y), and attach
    // coordinates to the nodes, and evaluate an integral \int_Omega f(x,y) dx dy
    Vec    v;
    double *av;
    int    m, numpts, *pts = NULL, dof, off;

    ierr = DMGetGlobalVector(dmplex, &v); CHKERRQ(ierr);
    ierr = PetscObjectSetName((PetscObject) v, "v"); CHKERRQ(ierr);
    ierr = VecSet(v,0.0); CHKERRQ(ierr);

    // FIXME Vec gets 1.0 for dofs on cell=1  <-- boring
    VecGetArray(v, &av);
    DMPlexGetTransitiveClosure(dmplex, 1, PETSC_TRUE, &numpts, &pts);
    for (j = 0; j < 2 * numpts; j += 2) {  // skip over orientations
        PetscSectionGetDof(section, pts[j], &dof);
        PetscSectionGetOffset(section, pts[j], &off);
        for (m = 0; m < dof; ++m) {
            av[off+m] = 1.0;
        }
    }
    DMPlexRestoreTransitiveClosure(dmplex, 1, PETSC_TRUE, &numpts, &pts);
    VecRestoreArray(v, &av);

    ierr = VecSetFromOptions(v); CHKERRQ(ierr);  // FIXME enables -v_vec_view?
    //ierr = PetscObjectViewFromOptions((PetscObject)v,NULL,"-vec_view"); CHKERRQ(ierr);
    ierr = DMRestoreGlobalVector(dmplex, &v); CHKERRQ(ierr);
#endif

    PetscSectionDestroy(&section); DMDestroy(&dmplex);
    return PetscFinalize();
}


/* This function is essentially equivalent to using DMPlexCreateFromCellList().
Note that rank 0 gets the actual mesh and other ranks get an empty mesh.
See the implementations of
    DMPlexBuildFromCellList_Private()
    DMPlexCreateFromCellListParallel()
    DMPlexInterpolate()
    DMPlexBuildCoordinates_Private()      */
PetscErrorCode CreateMeshByHand(DM *dmplex) {
    PetscErrorCode ierr;
    int           j;
    PetscMPIInt   rank;
    ierr = MPI_Comm_rank(PETSC_COMM_WORLD,&rank); CHKERRQ(ierr);
    ierr = DMPlexCreate(PETSC_COMM_WORLD,dmplex); CHKERRQ(ierr);
    ierr = DMSetDimension(*dmplex,dim); CHKERRQ(ierr);
    if (rank == 0) {
        // set the total number of points (npoint = ncell + nvert + nedges)
        ierr = DMPlexSetChart(*dmplex, 0, npoint); CHKERRQ(ierr);
        // the points are cells, vertices, edges in that order
        // we only set cones for cells and edges
        for (j = 0; j < ncell; j++) {
            ierr = DMPlexSetConeSize(*dmplex, j, dim+1); CHKERRQ(ierr);
        }
        for (j = ncell + nvert; j < npoint; j++) {
            ierr = DMPlexSetConeSize(*dmplex, j, dim); CHKERRQ(ierr);
        }
        ierr = DMSetUp(*dmplex);
        for (j = 0; j < ncell; j++) {
            ierr = DMPlexSetCone(*dmplex, j, ccone[j]); CHKERRQ(ierr);
        }
        for (j = ncell + nvert; j < npoint; j++) {
            ierr = DMPlexSetCone(*dmplex, j, econe[j-ncell-nvert]); CHKERRQ(ierr);
        }
    } else {
        ierr = DMPlexSetChart(*dmplex, 0, 0); CHKERRQ(ierr);
    }
    // with cones we have only upward directions and no labels for the strata
    // (note: both Symmetrize & Stratify are required, and they must be in this order
    ierr = DMPlexSymmetrize(*dmplex); CHKERRQ(ierr);
    ierr = DMPlexStratify(*dmplex); CHKERRQ(ierr);
    return 0;
}

// Set up a PetscSection which holds vertex coordinates.
PetscErrorCode CreateCoordinateSectionByHand(DM *dmplex) {
    PetscErrorCode ierr;
    PetscSection  coordSection;
    DM            cdm;
    Vec           coordinates;
    double        *acoord;
    int           j, d, dim, vertexstart, vertexend;
    // you have to setup the PetscSection returned by DMGetCoordinateSection() first,
    // or else the Vec returned by DMCreateLocalVector() has zero size
    // (and thus seg faults)
    ierr = DMGetDimension(*dmplex, &dim); CHKERRQ(ierr);
    ierr = DMGetCoordinateSection(*dmplex, &coordSection); CHKERRQ(ierr);
    ierr = DMPlexGetDepthStratum(*dmplex, 0, &vertexstart, &vertexend); CHKERRQ(ierr);
    ierr = PetscSectionSetNumFields(coordSection, 1); CHKERRQ(ierr);
    ierr = PetscSectionSetFieldComponents(coordSection, 0, dim); CHKERRQ(ierr);
    ierr = PetscSectionSetChart(coordSection, vertexstart, vertexend); CHKERRQ(ierr);
    for (j = vertexstart; j < vertexend; j++) {
        ierr = PetscSectionSetDof(coordSection, j, dim); CHKERRQ(ierr);
        ierr = PetscSectionSetFieldDof(coordSection, j, 0, dim); CHKERRQ(ierr);
    }
    ierr = PetscSectionSetUp(coordSection); CHKERRQ(ierr);
    // now we can actually set up the coordinate Vec
    ierr = DMGetCoordinateDM(*dmplex, &cdm); CHKERRQ(ierr);
    ierr = DMCreateLocalVector(cdm, &coordinates); CHKERRQ(ierr);
    ierr = VecSetBlockSize(coordinates,dim); CHKERRQ(ierr);
    ierr = PetscObjectSetName((PetscObject) coordinates, "coordinates"); CHKERRQ(ierr);
    ierr = VecGetArray(coordinates, &acoord); CHKERRQ(ierr);
    for (j = 0; j < vertexend-vertexstart; j++) {
        for (d = 0; d < dim; ++d) {
            acoord[j*dim+d] = coordverts[j*dim+d];
        }
    }
    ierr = VecRestoreArray(coordinates, &acoord); CHKERRQ(ierr);
    // finally we tell the DM that it has coordinates
    ierr = DMSetCoordinatesLocal(*dmplex, coordinates); CHKERRQ(ierr);
    VecDestroy(&coordinates);
    return 0;
}

// View dmplex, coordinate section, and vertex coordinates according to options.
PetscErrorCode PlexViewFromOptions(DM plex) {
    PetscErrorCode ierr;
    PetscBool  cell_cones = PETSC_FALSE,
               coords = PETSC_FALSE,
               ranges = PETSC_FALSE,
               use_height = PETSC_FALSE,
               vertex_supports = PETSC_FALSE;
    ierr = DMViewFromOptions(plex, NULL, "-dm_view"); CHKERRQ(ierr);  // why not enabled by default?
    ierr = PetscOptionsBegin(PETSC_COMM_WORLD, "plex_view_", "view options for tiny", "");CHKERRQ(ierr);
    ierr = PetscOptionsBool("-cell_cones", "print cones of each cell",
                            "tiny.c", cell_cones, &cell_cones, NULL);CHKERRQ(ierr);
    ierr = PetscOptionsBool("-coords", "print section and local vec for vertex coordinates",
                            "tiny.c", coords, &coords, NULL);CHKERRQ(ierr);
    ierr = PetscOptionsBool("-ranges", "print point index ranges for vertices,edges,cells",
                            "tiny.c", ranges, &ranges, NULL);CHKERRQ(ierr);
    ierr = PetscOptionsBool("-use_height", "use Height instead of Depth when printing points",
                            "tiny.c", use_height, &use_height, NULL);CHKERRQ(ierr);
    ierr = PetscOptionsBool("-vertex_supports", "print supports of each vertex",
                            "tiny.c", vertex_supports, &vertex_supports, NULL);CHKERRQ(ierr);
    ierr = PetscOptionsEnd();
    if (ranges) {
        ierr = PlexViewRanges(plex,use_height); CHKERRQ(ierr);
    }
    if (cell_cones) {
        ierr = PlexViewFans(plex,2,2,1); CHKERRQ(ierr);
    }
    if (vertex_supports) {
        ierr = PlexViewFans(plex,2,0,1); CHKERRQ(ierr);
    }
    if (coords) {
        PetscSection coordSection;
        Vec          coordVec;
        ierr = DMGetCoordinateSection(plex, &coordSection); CHKERRQ(ierr);
        if (coordSection) {
            ierr = PetscSectionView(coordSection,PETSC_VIEWER_STDOUT_WORLD); CHKERRQ(ierr);
        } else {
            ierr = PetscPrintf(PETSC_COMM_WORLD,
                "[vertex coordinates PetscSection has not been set]\n"); CHKERRQ(ierr);
        }
        ierr = DMGetCoordinatesLocal(plex,&coordVec); CHKERRQ(ierr);
        if (coords) {
            ierr = VecViewLocalStdout(coordVec,PETSC_COMM_WORLD); CHKERRQ(ierr);
        } else {
            ierr = PetscPrintf(PETSC_COMM_WORLD,
                "[vertex coordinates Vec not been set]\n"); CHKERRQ(ierr);
        }
    }
    return 0;
}

static const char* stratanames[4][10] =
                       {{"vertex","",    "",    ""},       // dim=0 names
                        {"vertex","cell","",    ""},       // dim=1 names
                        {"vertex","edge","cell",""},       // dim=2 names
                        {"vertex","edge","face","cell"}};  // dim=3 names

PetscErrorCode PlexViewRanges(DM plex, PetscBool use_height) {
    PetscErrorCode ierr;
    int         dim, m, start, end;
    MPI_Comm    comm;
    PetscMPIInt rank,size;
    ierr = PetscObjectGetComm((PetscObject)plex,&comm); CHKERRQ(ierr);
    ierr = MPI_Comm_size(comm,&size);CHKERRQ(ierr);
    ierr = MPI_Comm_rank(comm,&rank); CHKERRQ(ierr);
    ierr = DMGetDimension(plex,&dim); CHKERRQ(ierr);
    if (size > 1) {
        ierr = PetscSynchronizedPrintf(comm,"[rank %d] ",rank); CHKERRQ(ierr);
    }
    ierr = DMPlexGetChart(plex,&start,&end); CHKERRQ(ierr);
    ierr = PetscSynchronizedPrintf(comm,
        "chart for %d-dimensional DMPlex has points %d,...,%d\n",
        dim,start,end-1); CHKERRQ(ierr);
    for (m = 0; m < dim + 1; m++) {
        if (use_height) {
            ierr = DMPlexGetHeightStratum(plex,m,&start,&end); CHKERRQ(ierr);
            ierr = PetscSynchronizedPrintf(comm,
                "    height %d of size %d: %d,...,%d (%s)\n",
                m,end-start,start,end-1,dim < 4 ? stratanames[dim][2-m] : ""); CHKERRQ(ierr);
        } else {
            ierr = DMPlexGetDepthStratum(plex,m,&start,&end); CHKERRQ(ierr);
            ierr = PetscSynchronizedPrintf(comm,
                "    depth=dim %d of size %d: %d,...,%d (%s)\n",
                m,end-start,start,end-1,dim < 4 ? stratanames[dim][m] : ""); CHKERRQ(ierr);
        }
    }
    ierr = PetscSynchronizedFlush(comm,PETSC_STDOUT); CHKERRQ(ierr);
    return 0;
}

/* viewing cell cones in 2D:
     PlexViewFans(dmplex,2,2,1)
viewing vertex supports:
     PlexViewFans(dmplex,2,0,1)   */
PetscErrorCode PlexViewFans(DM plex, int dim, int basestrata, int targetstrata) {
    PetscErrorCode ierr;
    const int   *targets;
    int         j, m, start, end, cssize;
    MPI_Comm    comm;
    PetscMPIInt rank,size;
    ierr = PetscObjectGetComm((PetscObject)plex,&comm); CHKERRQ(ierr);
    ierr = MPI_Comm_size(comm,&size);CHKERRQ(ierr);
    ierr = MPI_Comm_rank(comm,&rank); CHKERRQ(ierr);
    if (size > 1) {
        ierr = PetscSynchronizedPrintf(comm,"[rank %d] ",rank); CHKERRQ(ierr);
    }
    ierr = PetscSynchronizedPrintf(comm,
        "%s (= %s indices) of each %s:\n",
        (basestrata > targetstrata) ? "cones" : "supports",
        stratanames[dim][targetstrata],stratanames[dim][basestrata]); CHKERRQ(ierr);
    ierr = DMPlexGetDepthStratum(plex, basestrata, &start, &end); CHKERRQ(ierr);
    for (m = start; m < end; m++) {
        if (basestrata > targetstrata) {
            ierr = DMPlexGetConeSize(plex,m,&cssize); CHKERRQ(ierr);
            ierr = DMPlexGetCone(plex,m,&targets); CHKERRQ(ierr);
        } else {
            ierr = DMPlexGetSupportSize(plex,m,&cssize); CHKERRQ(ierr);
            ierr = DMPlexGetSupport(plex,m,&targets); CHKERRQ(ierr);
        }
        ierr = PetscSynchronizedPrintf(comm,
            "    %s %d: ",stratanames[dim][basestrata],m); CHKERRQ(ierr);
        for (j = 0; j < cssize-1; j++) {
            ierr = PetscSynchronizedPrintf(comm,
                "%d,",targets[j]); CHKERRQ(ierr);
        }
        ierr = PetscSynchronizedPrintf(comm,
            "%d\n",targets[cssize-1]); CHKERRQ(ierr);
    }
    ierr = PetscSynchronizedFlush(comm,PETSC_STDOUT); CHKERRQ(ierr);
    return 0;
}

// for a local Vec, with components on each rank in gcomm, view each local part by rank
PetscErrorCode VecViewLocalStdout(Vec v, MPI_Comm gcomm) {
    PetscErrorCode ierr;
    int         m,locsize;
    PetscMPIInt rank,size;
    double      *av;
    const char  *vecname;
    ierr = MPI_Comm_size(gcomm,&size);CHKERRQ(ierr);
    ierr = MPI_Comm_rank(gcomm,&rank); CHKERRQ(ierr);
    ierr = PetscObjectGetName((PetscObject)v,&vecname); CHKERRQ(ierr);
    ierr = PetscPrintf(gcomm,"local Vec: %s %d MPI processes\n",
                       vecname,size); CHKERRQ(ierr);
    if (size > 1) {
        ierr = PetscSynchronizedPrintf(gcomm,"[rank %d]:\n",rank); CHKERRQ(ierr);
    }
    ierr = VecGetLocalSize(v,&locsize); CHKERRQ(ierr);
    ierr = VecGetArray(v, &av); CHKERRQ(ierr);
    for (m = 0; m < locsize; m++) {
        ierr = PetscSynchronizedPrintf(gcomm,"%g\n",av[m]); CHKERRQ(ierr);
    }
    ierr = VecRestoreArray(v, &av); CHKERRQ(ierr);
    ierr = PetscSynchronizedFlush(gcomm,PETSC_STDOUT); CHKERRQ(ierr);
    return 0;
}

