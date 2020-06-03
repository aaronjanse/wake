#!/usr/bin/env python3

'''
This is a wrapper around `fuse-wake` to provide caching.

DISCLAIMERS:
- this does not gracefully handle being killed while copying from cache
'''

import os
import sys
import json
import tempfile
import subprocess
import hashlib
import time
import shutil
import platform

from distutils.dir_util import copy_tree

def main():
    if not os.path.exists(get_cache_root()):
        os.makedirs(get_cache_root())
    if len(sys.argv)-1 != 2:
        print("Usage:")
        print("cached-wake <input.json path> <output.json path>")
        sys.exit(1)

    input_path = os.path.abspath(sys.argv[1])
    output_path = os.path.abspath(sys.argv[2])

    input_hash = get_input_hash(input_path)
    cache_dir = get_cache_dir(input_hash)
    cache_out = get_cache_out(input_hash)

    if not os.path.exists(cache_dir):
        os.makedirs(cache_dir)
        with tempfile.NamedTemporaryFile() as tmp_out:
            orig_cwd = os.getcwd()
            os.chdir(cache_dir)
            fuse_wake = os.path.join(os.path.dirname(__file__), 'fuse-wake')
            subprocess.call([fuse_wake, input_path, tmp_out.name], bufsize=4096, stdout=sys.stdout, stderr=sys.stderr)
            os.chdir(orig_cwd)

            shutil.copy(tmp_out.name, output_path)
            out_json = tmp_out.read()

        out_obj = json.loads(out_json)

        if out_obj['usage']['status'] != 0:
            shutil.rmtree(cache_dir)
            return # don't cache failed jobs
        
        out_obj['usage']['runtime'] = 0
        out_obj['usage']['cputime'] = 0
        out_obj['usage']['membytes'] = 0
        out_obj['usage']['inbytes'] = 0
        out_obj['usage']['outbytes'] = 0
        out_json = json.dumps(out_obj)

        with open(cache_out, "w") as f:
                f.write(out_json)
    else:
        # for concurrency
        while not os.path.exists(cache_out):
            time.sleep(1) # arbitrary time
        
        shutil.copy(cache_out, output_path)
    
    copy_all(cache_dir, os.getcwd())

def get_input_hash(input_path):
    with open(input_path, "r") as f:
        input_json = f.read()
    input_obj = json.loads(input_json)
    
    # print json in alphabetical order
    # yes, it's boring, but it's explicit and needs no pip dependencies
    std_json = '{'
    std_json += '"command":'
    std_json += json.dumps(input_obj['command'])
    std_json += ',"environment":'
    std_json += json.dumps(input_obj['environment'])
    std_json += ',"resources":'
    std_json += json.dumps(input_obj['resources'])
    std_json += ',"visible":['

    # sort visible files by filename
    input_obj['visible'].sort(key=lambda x: x.path)
    for (idx, v) in input_obj['visible']:
        # use json.dumps for strings to prevent injection attacks
        # (better safe than sorry)
        if idx > 0:
            std_json += ','
        std_json += '{"hash":'
        std_json += json.dumps(v['hash'])
        std_json += ',"path":'
        std_json += json.dumps(v['path'])
        std_json += '}'
    std_json += ']}'

    return hashlib.sha256(std_json.encode()).hexdigest()

def get_cache_dir(input_hash):
    return os.path.join(get_cache_root(), input_hash)

def get_cache_out(input_hash):
    return os.path.join(get_cache_root(), input_hash+"-out.json")

def get_cache_root():
    env = os.environ.get("WAKE_CACHE_LOCATION")
    if env is not None:
        return env # NOTE: does not verify if it's a valid path

    system = platform.system()
    if system == 'Darwin':
        # darwin does not allow adding directories to /
        home = os.path.expanduser("~")
        return os.path.join(home, ".wake-job-cache")
    else:
        return '/wake-job-cache'

# NOTE: this is not atomic!
# the `cp path/to/x /tmp/x ; mv /tmp/x .` trick may not work
# because this may be run on a machine with a small /tmp
# TODO: use /tmp when on darwin
def copy_all(src, dst):
    srcFiles = os.listdir(src)
    for filename in srcFiles:
        srcPath = os.path.join(src, filename)
        if srcPath == os.path.join(src, '.fuse.log'):
            continue
        if os.path.isdir(srcPath):
            copy_tree(srcPath, dst)
        else:
            shutil.copy(srcPath, dst)

main()
