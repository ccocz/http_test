import subprocess
import sys
import time
import urllib.parse
import os

cookies = sys.argv[1]
tested_address = sys.argv[2]


def call_raw():
    host = parsed.hostname
    port = parsed.port
    if port is None:
        port = 80
    netloc = host + ':' + str(port)
    os.system("./cmake-build-debug/http_test " + netloc + " " + cookies + " " + tested_address)


def https():
    host = parsed.hostname
    port = parsed.port
    if port is None:
        port = 443
    netloc = host + ':' + str(port)
    pid = "pid = " + os.path.abspath(os.getcwd()) + "/pid.info\n"
    file = open("stunnel.config", "w")
    file.write(pid)
    file.write("client = yes\n[service]\naccept = 127.0.0.1:3333\n")
    file.write("connect = " + netloc)
    netloc = "127.0.0.1:3333"
    file.close()
    os.system("sudo stunnel stunnel.config")
    time.sleep(2)
    os.system("./cmake-build-debug/http_test " + netloc + " " + cookies + " " + tested_address)
    file = open("pid.info", "r")
    pid = file.read()
    file.close()
    os.system("sudo kill " + pid)
    os.system("rm stunnel.config")


parsed = urllib.parse.urlparse(tested_address)

if parsed.scheme == "http":
    call_raw()
else:
    https()
