#!/bin/bash
#
# merge-mc.sh - Merge from mozilla-central to rust-frontend
#
# This requires three directories side by side:
#
# -   mozilla-central/ is an hg clone of mozilla-central
# -   mozilla-central-cinnabar/ is a git-cinnabar clone of mozilla-central
# -   rust-frontend/ is a git clone of rust-frontend
#
# rust-frontend and rust-frontend/js/jsparagus must be clean.
# The script refuses to run if you've got changes in rust-frontend/js/jsparagus.
# Updating rust-frontend to the current jsparagus revision is a separate step,
# something like: `git add js/jsparagus; git commit -m "Update jsparagus."`

set -eux

HG_REV=$(hg -R ../gecko log -r central --template '{node}')

if ! git diff --exit-code; then
    echo "local changes found, aborting"
    exit 1
fi

git checkout master
git fetch origin

MASTER_COMMIT=$(git rev-parse master)
ORIGIN_MASTER_COMMIT=$(git rev-parse origin/master)

if ! [ "${MASTER_COMMIT}" = "${ORIGIN_MASTER_COMMIT}" ]; then
    set +x
    echo
    echo 'The local `master` branch is not up-to-date with `origin/master`.'
    echo 'Try: git merge --ff-only origin/master'
    exit 1
fi

git checkout rust-frontend-merge-target

(
    cd ../mozilla-central-cinnabar
    if ! git diff --exit-code; then
        echo "local changes found in mozilla-central-cinnabar, aborting"
        exit 1
    fi

    echo "fetching from upstream to mozilla-central-cinnabar"
    git fetch origin

    GIT_REV=$(git cinnabar hg2git ${HG_REV})
    if [ "${GIT_REV}" = 0000000000000000000000000000000000000000 ]; then
        set +x; echo "hg revision ${HG_REV} not found in mozilla-central-cinnabar"
        exit 1
    fi
    set +x; echo "git commit hash is: ${GIT_REV}"; set -x

    git checkout rust-frontend-merge-target
    git merge --ff-only ${GIT_REV}
)

git pull --ff-only upstream rust-frontend-merge-target

set +x; echo "Fetched from upstream. Attempting merge..."; set -x
git checkout master
HG_REV_SHORT=$(hg -R ../gecko log -r "${HG_REV}" --template '{node|short}')
git merge rust-frontend-merge-target -m "Merge mozilla-central hg revision ${HG_REV_SHORT} to rust-frontend."
