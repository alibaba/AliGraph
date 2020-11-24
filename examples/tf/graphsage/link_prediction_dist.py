# Copyright 2020 Alibaba Group Holding Limited. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# =============================================================================
"""local training script for unsupervised GraphSage"""
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import os
import base64
import json
import sys
import numpy as np
import graphlearn as gl
import tensorflow as tf

from graph_sage import GraphSage


def load_graph(config):
  dataset_folder = config['dataset_folder']
  node_type = config['node_type']
  edge_type = config['edge_type']
  g = gl.Graph()\
        .node(dataset_folder + "node_table", node_type=node_type,
              decoder=gl.Decoder(attr_types=["float", "float", "string"]))\
        .edge(dataset_folder + "edge_table_train",
              edge_type=(node_type, node_type, edge_type),
              decoder=gl.Decoder(weighted=True), directed=True)\
        .node(dataset_folder + "node_table", node_type="train",
              decoder=gl.Decoder(attr_types=["float", "float", "string"]))
  return g


def train(config, graph):
  print('start training....')
  def model_fn():
    return GraphSage(graph,
                     config['class_num'],
                     config['features_num'],
                     config['batch_size'],
                     val_batch_size=config['val_batch_size'],
                     test_batch_size=config['test_batch_size'],
                     categorical_attrs_desc=config['categorical_attrs_desc'],
                     hidden_dim=config['hidden_dim'],
                     in_drop_rate=config['in_drop_rate'],
                     neighs_num=config['neighs_num'],
                     full_graph_mode=config['full_graph_mode'],
                     unsupervised=config['unsupervised'],
                     agg_type=config['agg_type'],
                     node_type=config['node_type'],
                     edge_type=config['edge_type'],
                     train_node_type=config['node_type'])

  cluster = tf.train.ClusterSpec({'ps': config['ps_hosts'], 'worker': config['worker_hosts']})
  trainer = gl.DistTFTrainer(model_fn,
                             cluster_spec=cluster,
                             task_name=config['job_name'],
                             task_index=config['task_index'],
                             epoch=config['epoch'],
                             optimizer=gl.get_tf_optimizer(
                                 config['learning_algo'],
                                 config['learning_rate'],
                                 config['weight_decay']))
  task_index = config['task_index']
  if config['job_name'] == 'worker': # also graph-learn client in this example.
    trainer.train()
    embs = trainer.get_node_embedding()
    np.save(config['emb_save_dir'] + str(task_index), embs)
    print("embds shape:", embs.shape)
    # ready_num = 0
    # while True:
    #     if ready_num == worker_num:
    #         break
    #     if os.path.exists('./ready_status_' + str(ready_num)):
    #         ready_num = ready_num + 1
    #    else:
    #         time.sleep(1)
    # print("start to test...")
    # test(config, graph, worker_num)
  else:
    trainer.join()


def main():
    config = {'dataset_folder': '../../data/ldbc_10k_people/',
            'class_num': 32,
            'features_num': 2,
            'batch_size': 10, # 10
            'categorical_attrs_desc': '',
            'hidden_dim': 256,
            'in_drop_rate': 0.5,
            'hops_num': 2,
            'neighs_num': [10, 20],
            'full_graph_mode': False,
            'agg_type': 'gcn',  # mean, sum
            'learning_algo': 'adam',
            'learning_rate': 0.005,
            'weight_decay': 0.0005,
            'epoch': 1,
            'unsupervised': True,
            'use_neg': True,
            'neg_num': 10,
            'emb_save_dir': './id_emb'}

    handle = sys.argv[1]
    index = int(sys.argv[2])
    debug = sys.argv[3]
    s = base64.b64decode(handle).decode('utf-8')
    obj = json.loads(s)
    node_type = obj['node_schema'][0].split(':')[0]
    edge_type = obj['edge_schema'][0].split(':')[1]
    config['node_type'] = node_type
    config['edge_type'] = edge_type
    config['train_node_type'] = node_type
    config['client_count'] = obj['client_count']

    # use the first half as worker, others as server
    servers = obj['server'].split(',')
    mid = len(servers) // 2
    config['worker_hosts'] = servers[0:mid]
    config['ps_hosts'] = servers[mid:]
    gl_hosts = []
    for index, ps_host in enumerate(config['ps_hosts']):

    obj['server'] = ','.join(config['ps_hosts'])
    obj['client'] = ','.join(config['worker_hosts'])
    print('worker_hosts', config['worker_hosts'])
    print('ps_hosts', config['ps_hosts'])
    if index < 2:
        config['job_name'] = 'worker'
        config['task_index'] = index
    else:
        config['job_name'] = 'ps'
        config['task_index'] = index - 2

    if config['job_name'] == 'ps':
        g = gl.init_graph_from_handle(obj, config['task_index'])
    else:
        g = gl.get_graph_from_handle(obj, worker_index=config['task_index'], worker_count=config['client_count'])

    """
    if debug == 'True' and config['job_name'] == 'worker':
        # s = g.node_sampler("train", batch_size=64)
        # nodes = s.get()
        nodes = g.V(node_type).batch(4).emit()
        print('nodes = ', nodes)
        print(nodes.ids)
        print(nodes.int_attrs)
        print(nodes.float_attrs)
        print(nodes.string_attrs)
        print("Get Nodes Done...")

        top = g.get_topology()
        res = "!!!!!!"
        res += str(top.get_src_type(edge_type))
        for k, v in top._topology.items():
            res += ("edge_type:" + k + ", src_type:" + v.src_type + \
                ", dst_type:" + v.dst_type + "\n")
        print('topology', res)
        with open("topology.txt", "w") as f:
            f.write(res)
    """

    train(config, g)

if __name__ == "__main__":
    main()
