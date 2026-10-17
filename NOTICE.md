# References and dependencies

The omp-postgresql code was written for this project. It contains no code copied from
SA-MP-MySQL (BSD-3-Clause) or omp-MySQL (MIT); both were examined as design references.

Dependencies fetched during the build or installed by the operator:

- [open.mp SDK](https://github.com/openmultiplayer/open.mp-sdk): see LICENSE.md at the pinned commit.
- [Pawn compiler and AMX headers](https://github.com/openmultiplayer/compiler): retain their original notices.
- [PostgreSQL/libpq](https://www.postgresql.org/about/licence/): PostgreSQL License.
- Transitive SDK dependencies (glm, robin-hood-hashing, span-lite, and string-view-lite): their respective licenses.
- OpenSSL and the C/C++ runtimes supplied with libpq: their distributors' notices and terms.

Local research clones, servers, and test artifacts are excluded by .gitignore.
Original project code is distributed under MIT (LICENSE). Also retain the notices for any
dependencies redistributed with the binaries.

The Windows libpq source build includes a narrow downstream correction to PostgreSQL's
mutex initialization. Modified PostgreSQL code retains the PostgreSQL License; packages
include PostgreSQL's COPYRIGHT, the applied patch and its rationale under
`share/omp-postgresql/patches/`. No reference-plugin source is involved.
