import numpy
arr = numpy.loadtxt("array.txt", usecols=range(1,2))
rb = numpy.loadtxt("rbtree.txt", usecols=range(1,2))
rbv2 = numpy.loadtxt("rbtreev2.txt", usecols=range(1,2))


print("array mean: {}".format(arr.mean()))
print("rbtree mean: {}".format(rb.mean()))
print("rbtree v2 mean: {}".format(rbv2.mean()))

