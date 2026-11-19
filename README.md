# RapidsSE-RoCE

Continued development of **RapidsSE, the Rapids Storage Engine**, with
[RoCEPool](https://github.com/RoCEPool/rocepool-fs) as its high-speed
distributed storage.

- **Origin.** This repository starts from the full history of upstream RapidsSE
  (Copyright (C) 2026 Boray Data), taken from the unmodified mirror
  [RoCEPool/RapidsSE](https://github.com/RoCEPool/RapidsSE) of
  https://bitbucket.org/rapidsdataproject/rapidsse/.
- **License.** Apache License 2.0, unchanged from upstream. See `LICENSE` and
  `COPYRIGHT`. Code under `thirdparty/` keeps its own licenses.
- **Branches.**
  - `main` -- development here. Starts at upstream `master`
    (`90012081f5a9`, 2026-09-22).
  - `master`, `releases/*` and all tags -- upstream, unmodified. They are
    updated only by `.github/workflows/sync-upstream.yml` (fast-forward only)
    and never receive commits from this project.
- **Taking upstream changes.** Open a pull request from `master` into `main`.

The original upstream README follows.

---

# RapidsSE
The Rapids Storage Engine

RapidsSE is an exploration project under RapidsDB.  It aims to build a very large scale, high performance, high throughput, highly reliable
storage engine, with Nested SQL support, and adopted a distributed extension of Unix pipe filter model as its core architecture style which
has successfully patented in US.

For further information contact dowson.liu@boraydata.com
