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

def make(dirname, version, target, build_command):
    global baserepo, CRLF;
    if os.path.exists(target):
        print('Target ' + target + ' exists, skipping rebuild.')
        return
    zipfile = dirname + '.zip'
    if not os.path.isdir(dirname):
        if not os.path.exists(zipfile):
            with open(zipfile,  'wb') as fp:
                baserepo.archive(fp, treeish=version, format='zip')
        archive = zf.ZipFile(zipfile, 'r')
        archive.extractall(dirname)
        archive.close()
    if not os.path.isfile(os.path.join(dirname, 'tools', 'build.py')):
        raise Exception('build.py does not exist in ' + dirname)
    with pushd(dirname) as wd:
        dstdir = os.path.dirname(target)
        blddir = wd.currd + '-build'
        print(wd.currd)
        if '--compile-flag=-m32' in build_command:
            tracerobject = TRACEROBJBASE + '-m32.o'
        else:
            tracerobject = TRACEROBJBASE + '.o'
        command = ['/usr/bin/python3'] \
                + CRLF.sub(' ', build_command).split(' ') \
                + ['--builddir=' + blddir,
                    '--compile-flag=-finstrument-functions',
                    '--linker-flag=' + os.path.join(wd.prevd, tracerobject),
                    '--link-lib=-lm'] \
                + DISABLE_WARNINGS
        if '-fsanitize=address' not in build_command:
            command += ['--linker-flag=-static']
        command = [ c for c in command if c ]
        sp.call(command)
        if not os.path.isfile(os.path.join(blddir, 'bin', 'jerry')):
            raise Exception('Could not build jerry in ' + blddir)
        if not os.path.isdir(dstdir):
            os.mkdir(dstdir)
        sh.copyfile(os.path.join(blddir, 'bin', 'jerry'), target)
        os.chmod(target, stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)
        sh.rmtree(blddir)

def run(jerry, testcase):
    EtM='../elf-to-map.py'
    CtG='../chain-to-graph.py'
    CGF='../convert-graph-formats.py'
    jbname = os.path.basename(jerry)
    if os.path.exists(jbname + '.dynamic.graphml.gz'):
        print('Graph for ' + jbname + 'exists, skipping execution.')
        return
    if not os.path.exists(jerry):
        raise Exception('Executable file ' + jerry + ' does not exist.')
    if os.path.exists('tracer' + TRACEFILEEXT):
        os.remove('tracer' + TRACEFILEEXT)
    idx = 0
    with open('temp.js', 'w') as testscript:
        testscript.write(testcase)
    sp.call([jerry, 'temp.js'])
    sh.move('tracer' + TRACEFILEEXT, jbname + TRACEFILEEXT)
    sp.call([CtG, '-b', '-m', '-g', jbname + TRACEFILEEXT])
    sp.call([EtM, jerry, jbname + '.dynamic.map'])
    sp.call([CGF, jbname + TRACEFILEEXT + '.all.graph.json', jbname + '.dynamic.graphml', '-m',  jbname + '.dynamic.map'])
    sp.call(['gzip', '-f', '-9', jbname + TRACEFILEEXT])
    sp.call(['gzip', '-f', '-9', jbname + TRACEFILEEXT + '.all.graph.json'])
    sp.call(['gzip', '-9', jbname + '.dynamic.graphml'])

baserepo = git.Repo('.')
TRACEFILEEXT='.bchains'
TRACEROBJBASE='tracerB'
DISABLE_WARNINGS=['--compile-flag=-Wno-return-type', '--compile-flag=-Wno-implicit-fallthrough']
CRLF = re.compile(r'( |\\|\r)*\n')

with open('jerry_test_security.json', 'r') as database:
    for bug in json.load(database):
        num = str(bug['issue'])
        trg = os.path.join(os.path.abspath(os.getcwd()), 'bin', 'jerry-i' + num);
        title('i' + num)
        try:
            make('../js-i' + num + '-bug', bug['rev_bug'], trg + '-bug', bug['bld_cmd'])
            run(trg + '-bug', bug['testcase'])
        except Exception as e:
            print(str(e))
            with open('secissuesbuild.log', 'at') as logfile:
                logfile.write("ERROR in reproducing issue %s (#%s)\n" % (num, bug['rev_bug']))
                logfile.write(str(e))
                logfile.write("--------\n")
        else:
            pass
        finally:
            pass
