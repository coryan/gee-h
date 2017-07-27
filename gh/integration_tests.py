#!/usr/bin/env python

import os
import socket
import subprocess
import time
import unittest

class Test(unittest.TestCase):
    @classmethod
    def tryConnect(cls, sleep, niter = 10):
        for _ in range(0, niter):
            sleep = sleep * 2
            time.sleep(1)
            try:
                _ = socket.create_connection(("localhost", "22379"), 0.5)
                print "etcd server is accepting connections"
                return True
            except socket.timeout:
                continue
            except socket.error:
                print " .. connecting to etcd failed"
        return False

    @classmethod
    def setUpClass(cls):
        if cls.tryConnect(0.01, 3):
            return
        print "initial connection failed, starting new etcd server instance"
        wnull = open(os.devnull, 'w')
        cls._etcd = subprocess.Popen(
            ["/usr/bin/etcd", "--listen-client-urls", "http://localhost:22379", "--advertise-client-urls",
             "http://localhost:22379"], stderr=wnull, stdout=wnull)
        if cls.tryConnect(0.05):
           return
        raise SystemExit("cannot connect nor start+connect the etcd server")

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, '_etcd') and cls._etcd is not None:
            cls._etcd.kill()
            cls._etcd.wait()
            cls._etcd = None

    @classmethod
    def __del__(cls):
        if hasattr(cls, '_etcd'):
            cls.tearDownClass()

    def test_session(self):
        try:
            text = subprocess.check_output(
                ["./gh_session_test"],
                stderr=subprocess.STDOUT)
            print text
        except subprocess.CalledProcessError as ex:
            print ex.output
            self.assertEqual(ex.returncode, 0)

    def disabled_test_leader_election(self):
        try:
            text = subprocess.check_output(
                ["gh/leader_election_test"],
                stderr=subprocess.STDOUT)
            print text
        except subprocess.CalledProcessError as ex:
            print ex.output
            self.assertEqual(ex.returncode, 0)

if __name__ == '__main__':
    unittest.main()
