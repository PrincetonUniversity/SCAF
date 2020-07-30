## SCAF: A Speculation-Aware Collaborative Dependence Analysis Framework

Note that the full implementation of SCAF is not yet integrated in this repository. The speculation modules along with a few other components are missing. The memory analysis modules are included though.

Disclaimer: this repository is part of a research project and it lacks industrial-level robustness in implementation (i.e., there will be bugs).

SCAF is customizable and can be used as a conservative memory analysis if the speculation modules are disabled.

The novelty, design, implementation, and evaluation of this work is described in the PLDI '20 paper by Apostolakis et al. titled "SCAF: A Speculation-Aware Dependence Analysis Framework" (https://dl.acm.org/doi/10.1145/3385412.3386028).

To reproduce the evaluation results presented in the PLDI 2020 paper, please refer to the artifact of the paper: [![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.3751586.svg)](https://doi.org/10.5281/zenodo.3751586)

This work builds upon CAF (CGO '17 by Johnson et al., https://dl.acm.org/doi/10.5555/3049832.3049849).

If you use SCAF in a publication, we would appreciate a citation to the PLDI '20 paper:

```
@inproceedings{apostolakis:2020:pldi,
author = {Apostolakis, Sotiris and Xu, Ziyang and Tan, Zujun and Chan, Greg and Campanoni, Simone and August, David I.},
title = {SCAF: A Speculation-Aware Collaborative Dependence Analysis Framework},
year = {2020},
isbn = {9781450376136},
publisher = {Association for Computing Machinery},
address = {New York, NY, USA},
url = {https://doi.org/10.1145/3385412.3386028},
doi = {10.1145/3385412.3386028},
booktitle = {Proceedings of the 41st ACM SIGPLAN Conference on Programming Language Design and Implementation},
pages = {638–654},
numpages = {17},
keywords = {collaboration, dependence analysis, speculation},
location = {London, UK},
series = {PLDI 2020}
}
```

### Prerequisites

LLVM 9.0.1

### Build SCAF
To build, run from the repository root directory: `make`

Run `make clean` from the root directory to clean the repository.
