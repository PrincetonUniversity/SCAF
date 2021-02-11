## SCAF: A Speculation-Aware Collaborative Dependence Analysis Framework

SCAF is an innovative dependence analysis framework that combines in a modular fashion memory analyses and (optionally) speculative assertions.

### Notice
The full implementation of SCAF is not yet integrated in this repository. All the currectly available memory analysis modules are included, but the speculation modules along with a few other components (e.g., profilers) are currently part of another repository (https://github.com/PrincetonUniversity/cpf).

### Publications
The novelty, design, implementation, and evaluation of this work is described in the PLDI '20 paper by Apostolakis et al. titled "SCAF: A Speculation-Aware Dependence Analysis Framework" ([ACM DL](https://dl.acm.org/doi/10.1145/3385412.3386028), [PDF](https://liberty.princeton.edu/Publications/pldi20_scaf.pdf), [Talk](https://youtu.be/kDaJaYB09p4), [Abstract](https://youtu.be/-fU0zDbpJWc)).

To reproduce the evaluation results presented in the PLDI 2020 paper, please refer to the artifact of the paper: [![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.3751586.svg)](https://doi.org/10.5281/zenodo.3751586)

This work builds upon CAF (CGO '17 by Johnson et al., [ACM DL](https://dl.acm.org/doi/10.5555/3049832.3049849)).

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

### Version Numbering Scheme

The version number is in the form of \[v _Major.Minor.Revision_ \]
- **Major**: Each major version matches a specific LLVM version (e.g., version 9 matches LLVM 9, version 11 matches LLVM 11)
- **Minor**: Starts from 0, each minor version represents either one or more API replacements/removals that might impact the users OR a forced update every six months (the minimum minor update frequency)
- **Revision**: Starts from 0; each revision version may include bug fixes or incremental improvements

#### Update Frequency

- **Major**: Matches the LLVM releases on a best-effort basis
- **Minor**: At least once per six months, at most once per month (1/month ~ 2/year)
- **Revision**: At least once per month, at most twice per week (2/week ~ 1/month)


### Prerequisites
LLVM 9.0.1

### Build SCAF
To build and install SCAF, run from the repository root directory: `make`

Run `make clean` from the root directory to clean the repository.

Run `make uninstall` from the root directory to uninstall SCAF.

### Configuration
SCAF is customizable and can be used as a conservative memory analysis if the speculation modules are disabled.

### Users
If you have any trouble using this framework feel free to reach out to us for help (contact liberators@liberty-research.org).

### Contributions
We welcome contributions from the community to improve this framework and evolve it to cater for more users.

### License
SCAF is licensed under the [MIT License](./LICENSE.TXT).
