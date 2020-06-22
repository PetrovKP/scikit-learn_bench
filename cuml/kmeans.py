# Copyright (C) 2020 Intel Corporation
#
# SPDX-License-Identifier: MIT

import argparse
from bench import (
    parse_args, measure_function_time, load_data, print_output
)
import numpy as np
from cuml import KMeans
import cuml
import warnings

cuml.common.logger = cuml.common.logger.level_critical


warnings.filterwarnings('ignore', category=FutureWarning)
parser = argparse.ArgumentParser(description='cuML K-means benchmark')
parser.add_argument('-i', '--filei', '--fileI', '--init',
                    type=str, help='Initial clusters')
parser.add_argument('-t', '--tol', type=float, default=0.,
                    help='Absolute threshold')
parser.add_argument('--maxiter', type=int, default=100,
                    help='Maximum number of iterations')
parser.add_argument('--samples-per-batch', type=int, default=32768,
                    help='Maximum number of iterations')
parser.add_argument('--n-clusters', type=int, help='Number of clusters')
params = parse_args(parser, prefix='cuml', loop_types=('fit', 'predict'))

# Load and convert generated data
X_train, X_test, _, _ = load_data(params)

# Load initial centroids from specified path
if params.filei is not None:
    X_init = np.load(params.filei).astype(params.dtype)
    params.n_clusters = X_init.shape[0]
# or choose random centroids from training data
else:
    np.random.seed(params.seed)
    centroids_idx = np.random.randint(0, X_train.shape[0],
                                      size=params.n_clusters)
    if hasattr(X_train, "iloc"):
        X_init = X_train.iloc[centroids_idx].to_pandas().values
    else:
        X_init = X_train[centroids_idx]

columns = ('batch', 'arch', 'prefix', 'function', 'threads', 'dtype', 'size',
           'n_clusters', 'time')


# Workaround for cuML kmeans fail
# when second call of 'fit' method causes AttributeError
def kmeans_fit(X):
    alg = KMeans(n_clusters=params.n_clusters, tol=params.tol,
                 max_iter=params.maxiter, init=X_init,
                 max_samples_per_batch=params.samples_per_batch)
    alg.fit(X)
    return alg


# Time fit
fit_time, kmeans = measure_function_time(kmeans_fit, X_train, params=params)
train_inertia = float(kmeans.inertia_)

# Time predict
predict_time, _ = measure_function_time(kmeans.predict, X_test, params=params)
test_inertia = float(kmeans.inertia_)

print_output(library='cuml', algorithm='kmeans',
             stages=['training', 'prediction'], columns=columns,
             params=params, functions=['KMeans.fit', 'KMeans.predict'],
             times=[fit_time, predict_time], accuracy_type='inertia',
             accuracies=[train_inertia, test_inertia], data=[X_train, X_test],
             alg_instance=kmeans)
