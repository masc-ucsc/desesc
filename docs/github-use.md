# desesc and GitHub

desesc is an architectural simulator that is primarily maintained and developed
by the [MASC lab][masc] at UC Santa Cruz.  Since desesc is used for computer
architecture research, the [MASC lab][masc] does development using a private
repo so that we can wait until the research is published before pushing changes
to the public repo hosted on GitHub.

This document describes the technique used at by the [MASC lab][masc] for maintaining
a private desesc repo and integrating changes with the public repo
that is hosted on GitHub.  Other groups may choose to adapt this
technique for their own use.

## Public desesc GitHub Repo

If you do not need the private repo, just get the public repo by executing:

    git clone https://github.com/masc-ucsc/desesc

## Private desesc MASC Lab Repo

If you are working on desesc at UC Santa Cruz, contact [Jose Renau](http://users.soe.ucsc.edu/~renau/)
to get access to the private desesc repo used by the MASC lab. The clone the private desesc Repo:

    git clone git@github.com:masc-ucsc/desesc-private.git desesc

If you are not tasked with synchronizing your work with the public repo then
you can simply push/pull changes to/from the private repo and someone else
will push them to the public one.

## Synchronizing Public and Private Repos

This section describes how we synchronize the public and private desesc repos.
Most users can ignore it and simply work on the appropriate public or private 
repo.

### Create Private desesc repo for first time

If you work outside UCSC, you should clone the public desesc repo. First, create
an private/public empty repository (desesc-private), then run this to close desesc

    # First
    git clone --bare https://github.com/masc-ucsc/desesc
    cd desesc.git
    git push --mirror https://github.com/yourname/desesc-private.git
    cd ..
    rm -rf desesc.git


### Typical usage

The workflow in the [MASC lab][masc] is as follows:

Work in the desesc-masc repo, and commit to main branch

    git clone https://github.com/yourname/desesc-private.git desesc
    cd desesc
    make some changes
    git commit
    git push origin main


To pull latest version of code from desesc public repository

    cd desesc
    git remote add public https://github.com/masc-ucsc/desesc
    git pull public main # Creates a merge commit
    git push origin main

To push your edits to the main public desesc repo (replace XXX by your github name)

    git clone https://github.com/masc-ucsc/desesc desesc-public
    cd desesc-public
    git remote add desesc-private git@github.com:masc-ucsc/desesc-masc.git  # Replace for your private repo
    git checkout -b pull_request_XXX
    git pull desesc-private main
    git push origin pull_request_XXX

Now create a [pull][pull] request through github, and the UCSC/MASC team will review it.

[pull]: https://help.github.com/articles/creating-a-pull-request
[masc]: http://masc.soe.ucsc.edu/
