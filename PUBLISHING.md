# Publish to GitHub

Three commands. Nothing here needs editing except the repository name if you
want a different one.

## 1. Create the repository

Empty, public, **no** README, **no** .gitignore, **no** license — the repo
already has all three and GitHub's generated ones will conflict.

Via the web UI: <https://github.com/new>

Or with the CLI:

```sh
gh repo create mount_ich --public --source=. --remote=origin
```

## 2. Push

```sh
cd mount_ich_open
git add -A
git commit -m "mount_ich 1.0.0"
git branch -M main
git remote add origin https://github.com/<YOUR-USERNAME>/mount_ich.git
git push -u origin main
```

If `gh repo create` was used, the remote already exists — skip the
`git remote add` line.

## 3. Tag the release

```sh
git tag -a v1.0.0 -m "mount_ich 1.0.0"
git push origin v1.0.0
```

Then on GitHub: Releases → Draft a new release → select `v1.0.0` → paste the
body of `RELEASE_NOTES.md` → publish.

---

## Before you push

**Confirm the commit identity.**

```sh
git config user.name   "vergilmorvx"
git config user.email  "<your-github-email>"
```

Commit authorship is part of the dated record. The repository's first push and
the commit dates are what a takedown request relies on. Set both before the
first commit, not after.

To attribute existing commits to the right identity:

```sh
git filter-branch --env-filter '
  export GIT_AUTHOR_NAME="vergilmorvx"
  export GIT_AUTHOR_EMAIL="<your-github-email>"
  export GIT_COMMITTER_NAME="vergilmorvx"
  export GIT_COMMITTER_EMAIL="<your-github-email>"
' -- --all
```

**The sample binary is not in this repository.** That is deliberate; see
`samples/README.md`. The analysis stands without it, since the hash identifies
the build and `docs/disassembly.txt` is committed.

**Don't squash the history.** A single initial commit is fine. A dated
sequence is slightly better, because it shows the work evolving rather than
appearing whole. Either way, push early — the push timestamp is the one a
platform will check.

---

## Repo description to paste into the About box

```
Ramdisk-side APFS volume mounter for jailbroken iOS (A12/A13). Reverse
engineering write-up, corrected implementation, and an itemised list of the
original's defects. Source-available, restricted license.
```

## Suggested topics

```
reverse-engineering  mach-o  arm64  ios  apfs  ramdisk  jailbreak
```

---

## Note on visibility

A public repository creates a dated, third-party-witnessed record of
authorship. That is the useful property: if the work ever appears somewhere
else under someone else's name, the publication date and contents are on file
with a neutral party and are not something either side can edit.

A private repository has no such record. It is fine for keeping the code to
yourself, but it gives you nothing to point at later.

Pick based on what you actually want. Neither is reversible in the way people
assume — a public repo can be deleted, but the publication date and content are
already recorded elsewhere by the time you would want to remove it.
