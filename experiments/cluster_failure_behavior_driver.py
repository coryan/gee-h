#!/usr/bin/env python3

import os
import socket
import subprocess
import time
import unittest
import pexpect
import sys


# Run a etcd cluster using the keys as client ports and values as peer ports.
CLUSTER_PORTS = {
    22379: 22380,
    27379: 27380,
    32379: 32380,
}


# Try to connect to a etcd server to verify it is running
def try_connect(port, sleep, niter=10):
    for _ in range(0, niter):
        time.sleep(sleep)
        sleep = sleep * 2
        try:
            s = socket.create_connection(("localhost", port), 0.5)
            print("etcd server is accepting connections on port=%d" % port)
            s.close()
            return True
        except socket.timeout:
            continue
        except socket.error:
            print(" .. connecting to etcd failed on port=%d" % port)
    return False


# Start a node of a etcd cluster
def start_node(port, peer_port, cluster_token, all_peer_urls):
    client_url = "http://localhost:%d" % port
    peer_url = "http://localhost:%d" % peer_port
    print("launching etcd on %s %s" % (client_url, peer_url))
    with open('node-%d.stdout.txt' % port, 'wb') as out, open('node-%d.stderr.txt' % port, 'wb') as err:
        etcd = subprocess.Popen(["/usr/bin/etcd",
                                 "--name", "node-%d" % port,
                                 "--listen-client-urls", client_url,
                                 "--advertise-client-urls", client_url,
                                 "--listen-peer-urls", peer_url,
                                 "--initial-advertise-peer-urls", peer_url,
                                 '--initial-cluster-token', cluster_token,
                                 '--initial-cluster', all_peer_urls,
                                 '--initial-cluster-state', 'new'],
                                stderr=err, stdout=out)
        return etcd


# Start a etcd cluster
def start_cluster(ports):
    cluster_token = 'test-cluster-01'
    all_peer_urls = ','.join(['node-%d=http://localhost:%d' % (k, v) for k, v in ports.items()])
    nodes = {}
    for k, v in ports.items():
        nodes[k] = start_node(k, v, cluster_token, all_peer_urls)
    return nodes


# Driver to test how etcd behaves when a cluster has partial and total failures.
class Test(unittest.TestCase):
    def test_cluster_failures(self):
        nodes = {}
        try:
            nodes = start_cluster(CLUSTER_PORTS)
            for port in nodes.keys():
                if not try_connect(port, 0.05):
                    raise SystemExit("cannot connect to etcd server on %d" % port)
            endpoints = 'http://' + ',http://'.join(['localhost:%d' % k for k in CLUSTER_PORTS.keys()])
            os.system('/usr/bin/env ETCDCTL_API=3 /usr/bin/etcdctl --endpoints=%s endpoint status' % endpoints)
            os.system('/usr/bin/env ETCDCTL_API=3 /usr/bin/etcdctl --endpoints=%s lease grant 60' % endpoints)
            address = ' '.join(['dns:localhost:%d' % k for k in CLUSTER_PORTS.keys()])
            program = pexpect.spawn('./cluster_failure_behavior prefix-test %s' % address,
                                    timeout=30, logfile=sys.stdout, encoding='UTF-8')
            print("Logfile set")
            program.expect('Client created\r\n')
            program.expect('Queue created\r\n')
            program.expect('Connected to etcd cluster.* continue\r\n')
            program.sendline('create')
            program.expect('Nodes created.* continue\r\n')
            primary = nodes[22379]
            primary.kill()
            primary.wait()
            nodes[22379] = None
            program.sendline('primary killed: update nodes')
            program.expect('Nodes updated.* continue\r\n')
            for k, v in nodes.items():
                if v is not None:
                    v.kill()
                    v.wait()
                    nodes[k] = None
            program.sendline('cluster killed: update nodes')
            program.expect('Node updates failed.* continue\r\n')
            nodes = start_cluster(CLUSTER_PORTS)
            program.sendline('delete nodes')
        except Exception as ex:
            print("Exception raised: %s" % ex)
        finally:
            for k, v in nodes.items():
                v.kill()
                v.wait()

if __name__ == '__main__':
    unittest.main()
