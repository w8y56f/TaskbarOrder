---
name: taskbarorder-release
description: Build the current TaskbarOrder version on Windows, create its password-protected delivery archive, and publish both assets to a versioned GitHub Release. Use when the user asks to build, package, release, or publish TaskbarOrder.
---

# TaskbarOrder Release

Run this workflow from the TaskbarOrder repository root. The root `VERSION`
file is the only version source. A valid release version is three decimal
components such as `0.1.0`; use tag `v<version>` and release title
`TaskbarOrder <version>`.

## 1. Check source state before building

1. Read `VERSION`, confirm it matches `^[0-9]+\.[0-9]+\.[0-9]+$`, and confirm
   `origin` points to the intended GitHub repository.
2. Fetch `origin` and inspect all of the following:
   - tracked and untracked working-tree changes;
   - commits in `HEAD` that are not on its upstream branch;
   - whether the upstream branch has commits missing locally.
3. If there are working-tree changes or unpushed commits, summarize them and
   ask the user whether to commit and push. Do not commit, push, build, tag, or
   publish until the user explicitly answers. Use a meaningful commit message
   based on the actual changes unless the user supplies one.
4. If the upstream is ahead or branches have diverged, stop and explain the
   exact state. Do not merge, rebase, reset, or force push as part of a release.
5. After user approval, commit the agreed files and push the current branch.
   Recheck that the working tree is clean and `HEAD` equals its upstream before
   continuing. If the repository was already clean and synchronized, proceed
   without asking a redundant question.

Treat the source commit as immutable after the build begins. Any subsequent
source change requires restarting this workflow from the source-state check.

## 2. Prevent accidental replacement

Before building, check both `git tag`/the remote and `gh release view` for
`v<version>`. If either already exists, stop and report it. Never overwrite,
delete, or reuse an existing release or tag.

## 3. Build a fresh Windows artifact

Use the repository's `.github/workflows/windows.yml` through
`workflow_dispatch` so the release comes from a fresh Windows x64 build of the
exact synchronized commit. Record the workflow run ID and commit SHA, wait for
completion, and require a successful conclusion.

Download the artifact named `TaskbarOrder-win11-x64`. Locate exactly one file
named:

```text
TaskbarOrder-<version>-win11-x64.zip
```

Reject a missing, differently versioned, empty, or ambiguous ZIP. Do not fall
back to an older file in the local `dist` directory when the fresh workflow
fails.

## 4. Create the protected delivery asset

Run:

```sh
.agents/skills/taskbarorder-release/scripts/package_release.sh \
  <downloaded-zip> <version> <release-output-directory>
```

The helper creates this layout:

```text
<release-output-directory>/
├── TaskbarOrder-<version>-win11-x64/
│   └── TaskbarOrder-<version>-win11-x64.zip
└── to.sa
```

`to.sa` is a 7z archive whose original name is
`TaskbarOrder-<version>-win11-x64.7z`. It contains the directory shown above,
uses password `Abc*123`, and encrypts filenames. The helper verifies the
archive with the same password. Do not upload the intermediate `.7z` name.

## 5. Publish GitHub Release

Create one GitHub Release against the exact built commit:

- Tag: `v<version>`
- Title: `TaskbarOrder <version>`
- Assets:
  - `TaskbarOrder-<version>-win11-x64.zip`
  - `to.sa`
- Notes: GitHub generated notes, supplemented only when the user provides
  release notes.

The ZIP is uploaded from inside its same-named staging directory. Use
`gh release create` with an explicit target commit so the release and tag refer
to the commit that produced the workflow artifact.

After publishing, use `gh release view` to verify the tag, target, release URL,
and both asset names. Report the release URL, commit SHA, ZIP SHA-256, and that
`to.sa` uses the requested password. If creation or upload fails, keep the
local artifacts and report the exact partial state; do not silently retry by
deleting or replacing remote objects.
