#!/bin/sh
# Pushes site/ (from tools/release.sh) as the gh-pages branch, which GitHub
# Pages serves at https://djb-rh.github.io/tabulous5/. The branch holds only
# the site, with its own history. Usage: tools/publish_site.sh <version>
set -e
ver=${1:?usage: tools/publish_site.sh <version>}
cd "$(dirname "$0")/.."
[ -f site/index.html ] || { echo "run tools/release.sh $ver first"; exit 1; }
work=$(mktemp -d)
if git show-ref --quiet refs/remotes/origin/gh-pages; then
  git worktree add -q "$work" gh-pages 2>/dev/null || git worktree add -q -B gh-pages "$work" origin/gh-pages
else
  git worktree add -q --detach "$work" && (cd "$work" && git checkout -q --orphan gh-pages && git rm -rfq . >/dev/null 2>&1 || true)
fi
rm -rf "$work"/*
cp -R site/. "$work"/
touch "$work/.nojekyll"
(cd "$work" && git add -A && git commit -q -m "Web installer for $ver" && git push -q origin gh-pages)
git worktree remove --force "$work"
echo "published: https://djb-rh.github.io/tabulous5/"
