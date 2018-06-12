#!/usr/bin/python3

import git
import json
import os
import re
import shutil as sh
import stat
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
    print('\033]0;' + str, end='\007\n')

def make(dirname, version, target):
    global baserepo, idregex;
    if os.path.exists(target):
        print('Target ' + target + ' exists, skipping rebuild.')
        return
    zipfile = dirname + '.zip'
    if not os.path.isdir(dirname):
        if not os.path.exists(zipfile):
            with open(dirname + '.zip',  'wb') as fp:
                baserepo.archive(fp, treeish=version, format='zip')
        archive = zf.ZipFile(dirname + '.zip', 'r')
        archive.extractall(dirname)
        archive.close()
    if not os.path.isfile(os.path.join(dirname, 'tools', 'build.py')):
        raise Exception('build.py does not exist in ' + dirname)
    with pushd(dirname) as wd:
        dstdir = os.path.dirname(target)
        blddir = wd.currd + '-build'
        print(wd.currd)
        sp.call(['/usr/bin/python3',
                os.path.join(wd.currd, 'tools', 'build.py'),
                '--clean',
                '--debug',
                '--builddir=' + blddir,
                '--jerry-libc=OFF',
                '--jerry-libm=OFF',
                '--jerry-ext=OFF',
                '--compile-flag=-finstrument-functions',
                '--linker-flag=' + os.path.join(wd.prevd, 'tracer.o'),
                '--link-lib=-lm'])
        if not os.path.isfile(os.path.join(blddir, 'bin', 'jerry')):
            raise Exception('Could not build jerry in ' + blddir)
        if not os.path.isdir(dstdir):
            os.mkdir(dstdir)
        sh.copyfile(os.path.join(blddir, 'bin', 'jerry'), target)
        os.chmod(target, stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)
        sh.rmtree(blddir)

def tests(dirname):
    global jsregex;
    for test in os.listdir(dirname):
        testname = os.path.join(dirname, test)
        if os.path.isfile(testname) and jsregex.match(test):
            yield testname
        elif os.path.isdir(testname):
            for subtest in tests(testname):
                yield subtest

def run(jerry):
    global idregex;
    EtM='../elf-to-map.py'
    TtG='../trace-to-graph.py'
    CGF='../convert-graph-formats.py'
    if not os.path.exists(jerry):
        raise Exception('Executable file ' + jerry + ' does not exist.')
    if os.path.exists('tracer.trc'):
        os.remove('tracer.trc')
    jbname = os.path.basename(jerry)
    idx = 0
    with open(jbname + '-passfail.data', 'w') as pfdata:
        for test in tests('tests/jerry'):
            idx += 1
            pfdata.write(str(idx) + (':PASS:' if sp.call([jerry, test]) == 0 else ':FAIL:') + test + '\n')
    sh.move('tracer.trc', jbname + '.trc')
    #sp.call([TtG, '-g', '-d', '-c', jbname + '.trc'])
    sp.call([TtG, '-m', '-g', '-d', '-c', jbname + '.trc'])
    sp.call([EtM, jerry, jbname + '.dynamic.map'])
    sp.call([CGF, jbname + '.trc.all.graph.json', jbname + '.dynamic.graphml', '-m',  jbname + '.dynamic.map'])
    sp.call(['gzip', jbname + '.trc'])
    sp.call(['gzip', jbname + '.trc.all.graph.json'])
    sp.call(['gzip', jbname + '.dynamic.graphml'])

baserepo = git.Repo('.')
idregex = re.compile(r'^regression-test-issues?-(.*)\.js$')
jsregex = re.compile(r'^(.*)\.js$')

with open('jerry_test_revset.json', 'r') as database:
    for bug in json.load(database):
        num = idregex.sub(r'\1', bug['filepath'])
        trg = os.path.join(os.path.abspath(os.getcwd()), 'bin', 'jerry-' + num);
        for sfx in ['bug', 'fix']:
            title(num + '::' + sfx)
            try:
                make('../js-' + num + '-' + sfx, bug['rev_' + sfx], trg + '-' + sfx)
                run(trg + '-' + sfx)
            except Exception as e:
                print(str(e))
            else:
                pass
            finally:
                pass
