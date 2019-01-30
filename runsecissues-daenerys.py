#!/usr/bin/python3

import datetime
import git
import json
import os
import re
import shutil as sh
import stat
import subprocess as sp
import tempfile
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

def break_chains(args):
    with tempfile.NamedTemporaryFile() as temp:
        sp.run([
                    'valgrind',
                    '--callgrind-out-file=' + temp.name,
                    '--tool=callgrind',
                    '--quiet',
                    '--separate-callers=1000', # FIXME: too high? too low?
                    '--separate-recs=1000', # FIXME: too high? too low?
                    '--skip-direct-rec=no',
                ] + args, timeout=30)
        with sp.Popen([
                    'callgrind_annotate',
                    '--threshold=100',
                    temp.name,
                ], stdout=sp.PIPE, universal_newlines=True) as callgrind_annotate:
            out, _ = callgrind_annotate.communicate()
    call_line_re = re.compile(r'\s*[0-9,.]+\s+\S+:(.+) \[')
    with open('callgrind.txt', 'w') as tracefile:
        tracefile.write('## START PROGRAM\n')
        for line in out.splitlines():
            line_match = call_line_re.match(line)
            if line_match:
                tracefile.write(LTOPRIVRE.sub('', '-->'.join(reversed(line_match.group(1).split("'"))))+'\n')
        tracefile.write('## QUIT PROGRAM\n')

def make(dirname, version, target, build_command):
    global baserepo;
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
        if not os.path.isdir(os.path.join(dirname, 'tools')):
            os.path.isdir(os.makedirs(dirname, 'tools'))
        sh.copyfile(os.path.join('tools', 'build.py'), os.path.join(dirname, 'tools', 'build.py'))
        sh.copyfile(os.path.join('tools', 'settings.py'), os.path.join(dirname, 'tools', 'settings.py'))
        #raise Exception('build.py does not exist in ' + dirname)
    with pushd(dirname) as wd:
        dstdir = os.path.dirname(target)
        blddir = wd.currd + '-build'
        print(wd.currd)
        command = ['/usr/bin/python3'] \
                + build_command.split(' ') \
                + ['--builddir=' + blddir] \
                + DISABLE_WARNINGS
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
    CtG='../chain-to-graph.py'
    CGF='../convert-graph-formats.py'
    jbname = os.path.basename(jerry)
    if os.path.exists(jbname + '.dynamic.graphml.gz'):
        print('Graph for ' + jbname + 'exists, skipping execution.')
        return
    if not os.path.exists(jerry):
        raise Exception('Executable file ' + jerry + ' does not exist.')
    idx = 0
    with open('temp.js', 'w') as testscript:
        testscript.write(testcase)
    break_chains([jerry, 'temp.js'])
    sh.move('callgrind.txt', jbname + TRACEFILEEXT)
    sp.call([CtG, '-m', '-g', jbname + TRACEFILEEXT])
    sp.call([CGF, jbname + TRACEFILEEXT + '.all.graph.json', jbname + '.dynamic.graphml'])
    sp.call(['gzip', '-f', '-9', jbname + TRACEFILEEXT])
    sp.call(['gzip', '-f', '-9', jbname + TRACEFILEEXT + '.all.graph.json'])
    sp.call(['gzip', '-9', jbname + '.dynamic.graphml'])

baserepo = git.Repo('.')
TRACEFILEEXT='.cgt'
DISABLE_WARNINGS=['--compile-flag=-Wno-return-type', '--compile-flag=-Wno-implicit-fallthrough']
LTOPRIVRE=re.compile(r'\.lto_priv\.[0-9]+')

with open('jerry_test_security.json', 'r') as database, open(datetime.datetime.now().strftime("build-nt-%Y%m%d-%H%M%S.log"), 'at') as logfile:
    for bug in json.load(database):
        num = str(bug['issue'])
        trg = os.path.join(os.path.abspath(os.getcwd()), 'bin', 'jerry-i' + num + 'nt');
        title('i' + num)
        try:
            bld = './tools/build.py --debug'
            if 'bld_cmd' in bug:
                bld = bug['bld_cmd']
            elif 'bld_typ' in bug:
                bld = {
                    'debug.linux' : './tools/build.py --clean --debug'
                }.get(bug['bld_typ'], bld)
            make('../js-i' + num + 'nt-bug', bug['rev_bug'], trg + '-bug', bld)
            run(trg + '-bug', bug['testcase'])
        except Exception as e:
            print(str(e))
            logfile.write("ERROR in reproducing issue %s (#%s)\n" % (num, bug['rev_bug']))
            logfile.write(str(e))
            logfile.write("\n--------\n")
        else:
            pass
        finally:
            pass
