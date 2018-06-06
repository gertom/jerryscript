#!/usr/bin/python3

import git
import json
import os
import re
import shutil as sh
import subprocess as sp
import zipfile as zf

class pushd:
    currd = None
    prevd = None
    def __init__(self, dirname):
        self.currd = os.path.abspath(dirname)
    def __enter__(self):
        self.prevd = os.getcwd()
        os.chdir(self.currd)
        return self
    def __exit__(self, type, value, traceback):
        os.chdir(self.prevd)

def title(str):
    print('\033]0;' + str, end='\007')

def make(dirname, version, name):
    global baserepo, idregex;
    with open(dirname + '.zip',  'wb') as fp:
        baserepo.archive(fp, treeish=version, format='zip')
    archive = zf.ZipFile(dirname + '.zip', 'r')
    archive.extractall(dirname)
    archive.close()
    with pushd(dirname) as wd:
        gitdir = wd.prevd
        dstdir = gitdir + '/bin'
        blddir = wd.currd + '-build'
        sp.call([gitdir + '/tools/build.py',
                '--clean',
                '--debug',
                '--builddir=' + blddir,
                '--jerry-libc=OFF',
                '--jerry-libm=OFF',
                '--jerry-ext=OFF',
                '--compile-flag=-finstrument-functions',
                '--linker-flag=' + wd.prevd + '/tracer.o',
                '--link-lib=-lm'])
        if not os.path.isdir(dstdir):
            os.mkdir(dstdir)
        sh.copyfile(blddir + '/bin/jerry', dstdir + '/jerry-' + name)
        sh.rmtree(blddir)

baserepo = git.Repo('.')
idregex = re.compile(r'^regression-test-issue-(.*)\.js$')


with open('jerry_test_revset.json') as database:
    for bug in json.load(database):
        num = idregex.sub(r'\1', bug['filepath'])
        title(num + '::bug')
        make('../js-' + num + '-bug', bug['rev_bug'], num + '-bug')
        title(num + '::fix')
        make('../js-' + num + '-fix', bug['rev_fix'], num + '-fix')
        exit()
