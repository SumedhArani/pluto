/*
 * PLUTO: An automatic parallelizer and locality optimizer
 * 
 * Copyright (C) 2007-2012 Uday Bondhugula
 *
 * This file is part of Pluto.
 *
 * Pluto is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * A copy of the GNU General Public Licence can be found in the file
 * `LICENSE' in the top-level directory of this distribution. 
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <getopt.h>
#include <libgen.h>

#include <unistd.h>
#include <sys/time.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


#include "osl/scop.h"
#include "osl/generic.h"
#include "osl/extensions/irregular.h"

#include "pluto.h"
#include "transforms.h"
#include "math_support.h"
#include "post_transform.h"
#include "program.h"
#include "version.h"

#include "clan/clan.h"
#include "candl/candl.h"
#include "candl/scop.h"

PlutoOptions *options;

void usage_message(void)
{
    fprintf(stdout, "Usage: polycc <input.c> [options] [-o output]\n");
    fprintf(stdout, "\nOptions:\n");
    fprintf(stdout, "       --tile                    Tile for locality\n");
    fprintf(stdout, "       --intratileopt            Optimize intra-tile execution order for locality\n");
    fprintf(stdout, "       --l2tile                  Tile a second time (typically for L2 cache) - disabled by default \n");
    fprintf(stdout, "       --parallel                Automatically parallelize (generate OpenMP)\n");
    fprintf(stdout, "     | --parallelize\n");
    fprintf(stdout, "       --lbtile | --diamond-tile Enables full dimensional concurrent start\n");
    fprintf(stdout, "       --partlbtile              Enables one-dimensional concurrent start\n");
    fprintf(stdout, "       --[no]prevector           Transform for and mark loops for (icc) vectorization (enabled by default)\n");
    fprintf(stdout, "       --innerpar                Choose pure inner parallelism over pipelined/wavefront parallelism\n");
    fprintf(stdout, "       --multipipe               Extract two or more degrees of pipelined parallelism if possible;\n");
    fprintf(stdout, "                                     by default one degree is extracted (if it exists)\n");
    fprintf(stdout, "       --rar                  Consider RAR dependences too (disabled by default)\n");
    fprintf(stdout, "       --[no]unroll           Unroll-jam (disabled by default)\n");
    fprintf(stdout, "       --ufactor=<factor>     Unroll-jam factor (default is 8)\n");
    fprintf(stdout, "       --[no]prevector        Make code amenable to compiler auto-vectorization (with ICC) - enabled by default\n");
    fprintf(stdout, "       --context=<context>    Parameters are at least as much as <context>\n");
    fprintf(stdout, "       --forceparallel=<depth>  Depth (1-indexed) to force parallel\n");
    fprintf(stdout, "       --isldep               Use ISL-based dependence tester\n");
    fprintf(stdout, "       --islsolve             Use ISL as ilp solver\n");
    fprintf(stdout, "       --readscop             Read input from a .scop file\n");
    fprintf(stdout, "       --lastwriter           Work with refined dependences (last conflicting access is computed for RAW/WAW)\n");
    fprintf(stdout, "       --bee                  Generate pragmas for Bee+Cl@k\n\n");
    fprintf(stdout, "       --indent  | -i         Indent generated code (disabled by default)\n");
    fprintf(stdout, "       --silent  | -q         Silent mode; no output as long as everything goes fine (disabled by default)\n");
    fprintf(stdout, "       --help    | -h         Print this help menu\n");
    fprintf(stdout, "       --version | -v         Display version number\n");
    fprintf(stdout, "\n   Fusion                Options to control fusion heuristic\n");
    fprintf(stdout, "       --nofuse                  Do not fuse across SCCs of data dependence graph\n");
    fprintf(stdout, "       --maxfuse                 Maximal fusion\n");
    fprintf(stdout, "       --smartfuse [default]     Heuristic (in between nofuse and maxfuse)\n");
    fprintf(stdout, "\n   Code generation       Options to control Cloog code generation\n");
    fprintf(stdout, "       --nocloogbacktrack        Do not call Cloog with backtrack (default - backtrack)\n");
    fprintf(stdout, "       --cloogsh                 Ask Cloog to use simple convex hull (default - off)\n");
    fprintf(stdout, "       --codegen-context=<context>       Parameters are at least as much as <context>\n");
    fprintf(stdout, "\n   Debugging\n");
    fprintf(stdout, "       --debug                   Verbose output\n");
    fprintf(stdout, "       --moredebug               More verbose output\n");
    fprintf(stdout, "\nTo report bugs, please email <pluto-development@googlegroups.com>\n\n");
}

static double rtclock()
{
    struct timezone Tzp;
    struct timeval Tp;
    int stat;
    stat = gettimeofday (&Tp, &Tzp);
    if (stat != 0) printf("Error return from gettimeofday: %d",stat);
    return(Tp.tv_sec + Tp.tv_usec*1.0e-6);
}

int main(int argc, char *argv[])
{
    int i;

    double t_start, t_c, t_d, t_t, t_all, t_start_all;

    t_c = 0.0;

    t_start_all = rtclock();

    FILE *src_fp;

    int option;
    int option_index = 0;

    int nolastwriter = 0;

    char *srcFileName;

    FILE *cloogfp, *outfp;

    if (argc <= 1)  {
        usage_message();
        return 1;
    }

    options = pluto_options_alloc();

    const struct option pluto_options[] =
    {
        {"tile", no_argument, &options->tile, 1},
        {"notile", no_argument, &options->tile, 0},
        {"intratileopt", no_argument, &options->intratileopt, 1},
        {"nointratileopt", no_argument, &options->intratileopt, 0},
        {"lbtile", no_argument, &options->lbtile, 1},
        {"diamond-tile", no_argument, &options->lbtile, 1},
        {"part-diamond-tile", no_argument, &options->partlbtile, 1},
        {"partlbtile", no_argument, &options->partlbtile, 1},
        {"debug", no_argument, &options->debug, true},
        {"moredebug", no_argument, &options->moredebug, true},
        {"rar", no_argument, &options->rar, 1},
        {"identity", no_argument, &options->identity, 1},
        {"nofuse", no_argument, &options->fuse, NO_FUSE},
        {"maxfuse", no_argument, &options->fuse, MAXIMAL_FUSE},
        {"smartfuse", no_argument, &options->fuse, SMART_FUSE},
        {"parallel", no_argument, &options->parallel, 1},
        {"parallelize", no_argument, &options->parallel, 1},
        {"innerpar", no_argument, &options->innerpar, 1},
        {"unroll", no_argument, &options->unroll, 1},
        {"nounroll", no_argument, &options->unroll, 0},
        {"polyunroll", no_argument, &options->polyunroll, 1},
        {"bee", no_argument, &options->bee, 1},
        {"ufactor", required_argument, 0, 'u'},
        {"prevector", no_argument, &options->prevector, 1},
        {"noprevector", no_argument, &options->prevector, 0},
        {"context", required_argument, 0, 'c'},
        {"cloogf", required_argument, 0, 'F'},
        {"cloogl", required_argument, 0, 'L'},
        {"cloogsh", no_argument, &options->cloogsh, 1},
        {"nocloogbacktrack", no_argument, &options->cloogbacktrack, 0},
        {"forceparallel", required_argument, 0, 'p'},
        {"ft", required_argument, 0, 'f'},
        {"lt", required_argument, 0, 'l'},
        {"multipipe", no_argument, &options->multipipe, 1},
        {"l2tile", no_argument, &options->l2tile, 1},
        {"version", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {"indent", no_argument, 0, 'i'},
        {"silent", no_argument, &options->silent, 1},
        {"lastwriter", no_argument, &options->lastwriter, 1},
        {"nolastwriter", no_argument, &nolastwriter, 1},
        {"nobound", no_argument, &options->nobound, 1},
        {"scalpriv", no_argument, &options->scalpriv, 1},
        {"isldep", no_argument, &options->isldep, 1},
        {"candldep", no_argument, &options->candldep, 1},
        {"isldepcompact", no_argument, &options->isldepcompact, 1},
        {"readscop", no_argument, &options->readscop, 1},
        {"islsolve", no_argument, &options->islsolve, 1},
        {"time", no_argument, &options->time, 1},
        {0, 0, 0, 0}
    };


    /* Read command-line options */
    while (1) {
        option = getopt_long(argc, argv, "bhiqvf:l:F:L:c:o:", pluto_options,
                &option_index);

        if (option == -1)   {
            break;
        }

        switch (option) {
            case 0:
                break;
            case 'F':
                options->cloogf = atoi(optarg);
                break;
            case 'L':
                options->cloogl = atoi(optarg);
                break;
            case 'b':
                options->bee = 1;
                break;
            case 'c':
                options->codegen_context = atoi(optarg);
                break;
            case 'd':
                break;
            case 'f':
                options->ft = atoi(optarg);
                break;
            case 'g':
                break;
            case 'h':
                usage_message();
                return 2;
            case 'i':
                /* Handled in polycc */
                break;
            case 'l':
                options->lt = atoi(optarg);
                break;
            case 'm':
                break;
            case 'n':
                break;
            case 'o':
                options->out_file = strdup(optarg);
                break;
            case 'p':
                options->forceparallel = atoi(optarg);
                break;
            case 'q':
                options->silent = 1;
                break;
            case 's':
                break;
            case 'u':
                options->ufactor = atoi(optarg);
                break;
            case 'v':
                printf("PLUTO %s - An automatic parallelizer and locality optimizer\n\
Copyright (C) 2007--2008  Uday Kumar Bondhugula\n\
This is free software; see the source for copying conditions.  There is NO\n\
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n\n", PLUTO_VERSION);
                pluto_options_free(options);
                return 3;
            default:
                usage_message();
                pluto_options_free(options);
                return 4;
        }
    }

    if (optind <= argc-1)   {
        srcFileName = alloca(strlen(argv[optind])+1);
        strcpy(srcFileName, argv[optind]);
    }else{
        /* No non-option argument was specified */
        usage_message();
        pluto_options_free(options);
        return 5;
    }

    /* Make options consistent */
    if (options->isldep && options->candldep) {
        printf("[pluto] ERROR: only one of isldep and candldep should be specified)\n");
        pluto_options_free(options);
        usage_message();
        return 1;
    }

    /* isldep is the default */
    if (!options->isldep && !options->candldep) {
        options->isldep = 1;
    }

    if (options->lastwriter && options->candldep) {
        printf("[pluto] ERROR: --lastwriter is only supported with --isldep\n");
        pluto_options_free(options);
        usage_message();
        return 1;
    }

    if (options->identity == 1) {
        options->partlbtile = 0;
        options->lbtile = 0;
    }

    if (options->partlbtile == 1 && options->lbtile == 0)    {
        options->lbtile = 1;
    }

    if (options->lbtile == 1 && options->tile == 0)    {
        options->tile = 1;
    }

    if (options->multipipe == 1 && options->parallel == 0)    {
        fprintf(stdout, "Warning: multipipe needs parallel to be on; turning on parallel\n");
        options->parallel = 1;
    }

    if (options->multipipe == 1 && options->parallel == 0)    {
        fprintf(stdout, "Warning: multipipe needs parallel to be on; turning on parallel\n");
        options->parallel = 1;
    }

    /* Disable pre-vectorization if tile is not on */
    if (options->tile == 0 && options->prevector == 1) {
        /* If code will not be tiled, pre-vectorization does not make
         * sense */
        if (!options->silent)   {
            fprintf(stdout, "[Pluto] Turning off pre-vectorization (--tile is off)\n");
        }
        options->prevector = 0;
    }


    /* Extract polyhedral representation */
    PlutoProg *prog = NULL; 

    osl_scop_p scop = NULL;
    char *irroption = NULL;

    /* Extract polyhedral representation from clan scop */
    if(!strcmp(srcFileName, "stdin")){  //read from stdin
        src_fp = stdin;
        osl_interface_p registry = osl_interface_get_default_registry();
        t_start = rtclock();
        scop = osl_scop_pread(src_fp, registry, PLUTO_OSL_PRECISION);
        t_d = rtclock() - t_start;
    }else{  // read from regular file

      src_fp  = fopen(srcFileName, "r");

      if (!src_fp)   {
          fprintf(stderr, "pluto: error opening source file: '%s'\n", srcFileName);
          pluto_options_free(options);
          return 6;
      }

      clan_options_p clanOptions = clan_options_malloc();

      if (options->readscop){
          osl_interface_p registry = osl_interface_get_default_registry();
          t_start = rtclock();
          scop = osl_scop_pread(src_fp, registry, PLUTO_OSL_PRECISION);
          t_d = rtclock() - t_start;
      }else{
          t_start = rtclock();
          scop = clan_scop_extract(src_fp, clanOptions);
          t_d = rtclock() - t_start;
      }

      if (!scop || !scop->statement)   {
          fprintf(stderr, "Error extracting polyhedra from source file: \'%s'\n",
                  srcFileName);
          pluto_options_free(options);
          return 8;
      }
      FILE *srcfp = fopen(".srcfilename", "w");
      if (srcfp)    {
          fprintf(srcfp, "%s\n", srcFileName);
          fclose(srcfp);
      }

      clan_options_free(clanOptions);

      /* IF_DEBUG(clan_scop_print_dot_scop(stdout, scop, clanOptions)); */
  
    }

    /* Convert clan scop to Pluto program */
    prog = scop_to_pluto_prog(scop, options);

    /* Backup irregular program portion in .scop. */
    osl_irregular_p irreg_ext = NULL;
    irreg_ext = osl_generic_lookup(scop->extension, OSL_URI_IRREGULAR);
    if(irreg_ext!=NULL)
      irroption = osl_irregular_sprint(irreg_ext);  //TODO: test it
    osl_irregular_free(irreg_ext);

    IF_MORE_DEBUG(pluto_prog_print(stdout, prog));

    int dim_sum=0;
    for (i=0; i<prog->nstmts; i++) {
        dim_sum += prog->stmts[i]->dim;
    }

   
    if (!options->silent)   {
        fprintf(stdout, "[Pluto] Number of statements: %d\n", prog->nstmts);
        fprintf(stdout, "[Pluto] Total number of loops: %d\n", dim_sum);
        fprintf(stdout, "[Pluto] Number of deps: %d\n", prog->ndeps);
        fprintf(stdout, "[Pluto] Maximum domain dimensionality: %d\n", prog->nvar);
        fprintf(stdout, "[Pluto] Number of parameters: %d\n", prog->npar);
    }
    t_start = rtclock();
    /* Auto transformation */
    if (!options->identity) {
        pluto_auto_transform(prog);
    }
    t_t = rtclock() - t_start;
    pluto_detect_transformation_properties(prog);

    if (!options->silent)   {
        fprintf(stdout, "[Pluto] Affine transformations [<iter coeff's> <param> <const>]\n\n");
        /* Print out transformations */
        pluto_transformations_pretty_print(prog);
        pluto_print_hyperplane_properties(prog);
    }

    if (options->tile)   {
        pluto_tile(prog);

        if (options->lbtile) {
            pluto_reschedule_tile(prog);
        }
    }else{
        if (options->intratileopt) {
            pluto_intra_tile_optimize(prog, 0);
        }
    }

    if (options->parallel && !options->tile && !options->identity)   {
        /* Obtain wavefront/pipelined parallelization by skewing if
         * necessary */
        int nbands;
        Band **bands;
        bands = pluto_get_outermost_permutable_bands(prog, &nbands);
        bool retval = pluto_create_tile_schedule(prog, bands, nbands);
        pluto_bands_free(bands, nbands);

        /* If the user hasn't supplied --tile and there is only pipelined
         * parallelism, we will warn the user */
        if (retval)   {
            printf("[Pluto] WARNING: pipelined parallelism exists and --tile is not used.\n");
            printf("use --tile for better parallelization \n");
            IF_DEBUG(fprintf(stdout, "[Pluto] After skewing:\n"););
            IF_DEBUG(pluto_transformations_pretty_print(prog););
            IF_DEBUG(pluto_print_hyperplane_properties(prog););
        }
    }

    if (options->unroll || options->polyunroll)    {
        /* Will generate a .unroll file */
        /* plann/plorc needs a .params */
        FILE *paramsFP = fopen(".params", "w");
        if (paramsFP)   {
            int i;
            for (i=0; i<prog->npar; i++)  {
                fprintf(paramsFP, "%s\n", prog->params[i]);
            }
            fclose(paramsFP);
        }
        pluto_detect_mark_unrollable_loops(prog);
    }

    if (options->polyunroll)    {
        /* Experimental */
        for (i=0; i<prog->num_hyperplanes; i++)   {
            if (prog->hProps[i].unroll)  {
                unroll_phis(prog, i, options->ufactor);
            }
        }
    }

    if(!strcmp(srcFileName, "stdin")){  
        //input stdin == output stdout
        pluto_populate_scop(scop, prog, options);
        osl_scop_print(stdout, scop);
    }else{  // do the usual Pluto stuff
  
      /* NO MORE TRANSFORMATIONS BEYOND THIS POINT */
      /* Since meta info about loops
       * is printed to be processed by scripts - if transformations are
       * performed, changed loop order/iterator names will be missed  */
      gen_unroll_file(prog);
  
      char *outFileName;
      char *cloogFileName;
      if (options->out_file == NULL)  {
          /* Get basename, remove .c extension and append a new one */
          char *basec, *bname;
          basec = strdup(srcFileName);
          bname = basename(basec);
  
          /* max size when tiled.* */
          outFileName = alloca(strlen(bname)+strlen(".pluto.c")+1);
          cloogFileName = alloca(strlen(bname)+strlen(".pluto.cloog")+1);
  
          if (strlen(bname) >= 2 && !strcmp(bname+strlen(bname)-2, ".c")) {
              strncpy(outFileName, bname, strlen(bname)-2);
              strncpy(cloogFileName, bname, strlen(bname)-2);
              outFileName[strlen(bname)-2] = '\0';
              cloogFileName[strlen(bname)-2] = '\0';
          }else{
              strcpy(outFileName, bname);
              strcpy(cloogFileName, bname);
          }
          strcat(outFileName, ".pluto.c");
          free(basec);
      }else{
          outFileName = options->out_file;
          cloogFileName = alloca(strlen(options->out_file)+1);
          strcpy(cloogFileName, options->out_file);
      }
  
      strcat(cloogFileName, ".pluto.cloog");
  
      cloogfp = fopen(cloogFileName, "w+");
      if (!cloogfp)   {
          fprintf(stderr, "[Pluto] Can't open .cloog file: '%s'\n", cloogFileName);
          pluto_options_free(options);
          pluto_prog_free(prog);
          return 9;
      }
  
      outfp = fopen(outFileName, "w");
      if (!outfp) {
          fprintf(stderr, "[Pluto] Can't open file '%s' for writing\n", outFileName);
          pluto_options_free(options);
          pluto_prog_free(prog);
          fclose(cloogfp);
          return 10;
      }
  
      /* Generate .cloog file */
      pluto_gen_cloog_file(cloogfp, prog);
      /* Add the <irregular> tag from clan, if any */
      if (irroption != NULL) {
          fprintf(cloogfp, "<irregular>\n%s\n</irregular>\n\n", irroption);
          free(irroption);
      }
  
      rewind(cloogfp);
  
  
      /* Generate code using Cloog and add necessary stuff before/after code */
      t_start = rtclock();
      pluto_multicore_codegen(cloogfp, outfp, prog);
      t_c = rtclock() - t_start;
  
      FILE *tmpfp = fopen(".outfilename", "w");
      if (tmpfp)    {
          fprintf(tmpfp, "%s\n", outFileName);
          fclose(tmpfp);
          printf( "[Pluto] Output written to %s\n", outFileName);
      }
  
      fclose(cloogfp);
      fclose(outfp);

    }


    t_all = rtclock() - t_start_all;

    if (options->time) {
        printf("\n[pluto] Timing statistics\n[pluto] SCoP extraction + dependence analysis time: %0.6lfs\n", t_d);
        printf("[pluto] Auto-transformation time: %0.6lfs\n", t_t);
        printf("[pluto] Code generation time: %0.6lfs\n", t_c);
        printf("[pluto] Other/Misc time: %0.6lfs\n", t_all-t_c-t_t-t_d);
        printf("[pluto] Total time: %0.6lfs\n", t_all);
        printf("[times] %0.6lf %0.6lf %.6lf %.6lf\n", t_d, t_t, t_c,
             t_all-t_c-t_t-t_d);
    }

    pluto_prog_free(prog);
    pluto_options_free(options);

    osl_scop_free(scop);

    return 0;
}
