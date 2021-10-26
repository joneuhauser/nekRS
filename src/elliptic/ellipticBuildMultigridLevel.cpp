/*

   The MIT License (MIT)

   Copyright (c) 2017 Tim Warburton, Noel Chalmers, Jesse Chan, Ali Karakus

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

 */

#include "elliptic.h"
#include "platform.hpp"

namespace{

std::string gen_suffix(const elliptic_t * elliptic, const char * floatString)
{
  const std::string precision = std::string(floatString);
  if(precision.find(pfloatString) != std::string::npos){
    return std::string("_") + std::to_string(elliptic->mesh->N) + std::string("pfloat");
  }
  else{
    return std::string("_") + std::to_string(elliptic->mesh->N);
  }
  
}

}

elliptic_t* ellipticBuildMultigridLevel(elliptic_t* baseElliptic, int Nc, int Nf)
{
  
  elliptic_t* elliptic = new elliptic_t();
  memcpy(elliptic,baseElliptic,sizeof(elliptic_t));

  mesh_t* mesh = createMeshMG(baseElliptic->mesh, Nc);
  elliptic->mesh = mesh;

  int verbose = elliptic->options.compareArgs("VERBOSE","TRUE") ? 1:0;
  meshParallelGatherScatterSetup(mesh, mesh->Nlocal, mesh->globalIds, platform->comm.mpiComm, verbose);

  { // setup an unmasked gs handle
    ogs_t *ogs = NULL;
    ellipticOgs(mesh, mesh->Nlocal, /* nFields */ 1, /* offset */ 0, elliptic->BCType, /* BCTypeOffset */ 0,
                elliptic->Nmasked, elliptic->o_mapB, elliptic->o_maskIds, &ogs);
    elliptic->ogs = ogs;
    elliptic->o_invDegree = elliptic->ogs->o_invDegree;
  }

  std::string suffix = "Hex3D";

  std::string kernelName;

  MPI_Barrier(platform->comm.mpiComm);
  double tStartLoadKernel = MPI_Wtime();

  ellipticBuildPreconditionerKernels(elliptic);

  const std::string poissonPrefix = elliptic->poisson ? "poisson-" : "";

  {
      // check for trilinear
      if(elliptic->elementType != HEXAHEDRA) {
        kernelName = "ellipticPartialAx" + suffix;
      }else {
        if(elliptic->options.compareArgs("ELEMENT MAP", "TRILINEAR"))
          kernelName = "ellipticPartialAxTrilinear" + suffix;
        else
          kernelName = "ellipticPartialAx" + suffix;
      }

      {
        const std::string kernelSuffix = gen_suffix(elliptic, dfloatString);
        elliptic->AxKernel = platform->kernels.getKernel(poissonPrefix + kernelName + kernelSuffix);
      }
      if(!strstr(pfloatString,dfloatString)) {
        const std::string kernelSuffix = gen_suffix(elliptic, pfloatString);
        elliptic->AxPfloatKernel =
          platform->kernels.getKernel(poissonPrefix + kernelName + kernelSuffix);
      }
  }

  elliptic->precon = new precon_t();

  {
    const std::string kernelSuffix = std::string("_") + std::to_string(Nf);

    kernelName = "ellipticPreconCoarsen" + suffix;
    elliptic->precon->coarsenKernel = platform->kernels.getKernel(kernelName + kernelSuffix);
    kernelName = "ellipticPreconProlongate" + suffix;
    elliptic->precon->prolongateKernel = platform->kernels.getKernel(kernelName + kernelSuffix);

  }

  MPI_Barrier(platform->comm.mpiComm);
  if(platform->comm.mpiRank == 0) printf("done (%gs)\n", MPI_Wtime() - tStartLoadKernel);
  fflush(stdout);

  return elliptic;
}
