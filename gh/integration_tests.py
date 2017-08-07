#!/usr/bin/env python3

import socket
import subprocess
import time
import unittest


def try_connect(sleep, niter=10):
    for _ in range(0, niter):
        time.sleep(sleep)
        sleep = sleep * 2
        try:
            s = socket.create_connection(("localhost", "22379"), 0.5)
            print("etcd server is accepting connections")
            s.close()
            return True
        except socket.timeout:
            continue
        except socket.error:
            print(" .. connecting to etcd failed")
    return False


class Test(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        if try_connect(0.01, 3):
            return
        print("initial connection failed, starting new etcd server instance")
        cls._etcd = subprocess.Popen(
            ["/usr/bin/etcd", "--listen-client-urls", "http://localhost:22379", "--advertise-client-urls",
             "http://localhost:22379"], stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
        if not try_connect(0.05):
            raise SystemExit("cannot connect nor start+connect the etcd server")

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, '_etcd') and cls._etcd is not None:
            cls._etcd.kill()
            cls._etcd.wait()
            cls._etcd = None

    def test_session(self):
        try:
            text = subprocess.check_output(["./gh_session_test"], stderr=subprocess.STDOUT)
            print(str(text, encoding='UTF-8'))
        except subprocess.CalledProcessError as ex:
            print(ex.output)
            self.assertEqual(ex.returncode, 0)

    def test_leader_election(self):
        try:
            text = subprocess.check_output(["./gh_leader_election_test"], stderr=subprocess.STDOUT)
            print(str(text, encoding='UTF-8'))
        except subprocess.CalledProcessError as ex:
            print(ex.output)
            self.assertEqual(ex.returncode, 0)

    def test_watch_election(self):
        try:
            text = subprocess.check_output(["./gh_watch_election_test"], stderr=subprocess.STDOUT)
            print(str(text, encoding='UTF-8'))
        except subprocess.CalledProcessError as ex:
            print(ex.output)
            self.assertEqual(ex.returncode, 0)

if __name__ == '__main__':
    unittest.main()
