# Runtime integration patches

These research patches are a snapshot of local work against two specific
upstream source revisions. They are not a standalone runtime, and they do not
include the generated game module, game data, platform dependencies or a
playable build.

| Patch | Upstream | Base revision |
| --- | --- | --- |
| `moderngekko.patch` | [ModernGekko](https://github.com/ExpansionPak/ModernGekko) | `edd460adad0e82b56909af1d793642c83b6d31c8` |
| `recompcore.patch` | [RecompCore](https://github.com/ExpansionPak/RecompCore) | `10f7ac4fe48f74485fbc161ebf4d67242e11e671` |

The ModernGekko patch includes a native-only runtime option, integration and
automation changes, plus an optional native-only test that requires the user's
own compatible game data. The RecompCore patch contains the corresponding boot,
static dispatch, memory, input and macOS platform changes. Their application
was checked against the exact recorded base revisions. This does not establish
that the patches build on other revisions or that a complete game works.

Check and apply each patch in a **clean checkout of the matching upstream
repository**, using an absolute path to this snapshot:

```sh
git apply --check /path/to/smg2-reverse-engineering/patches/moderngekko.patch
git apply /path/to/smg2-reverse-engineering/patches/moderngekko.patch
```

Run the equivalent commands for `recompcore.patch` from the root of the
RecompCore checkout. These are separate repositories; do not apply both patches
at the same root. The upstream commits, submodules and required tools must be
obtained independently.

ModernGekko's license text is included as
`LICENSE-ModernGekko-GPL-3.0.txt`. RecompCore's `COPYING` notice and its
per-file SPDX license texts are reproduced in `COPYING-RecompCore.txt` and
`LICENSES/`. The patches modify code governed by those upstream licenses;
consult the upstream repository and individual file notices when reusing them.
This does not imply a license for the separately authored native modules in
the root of this repository.
