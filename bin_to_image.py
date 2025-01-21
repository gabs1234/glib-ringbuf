import numpy as np
import matplotlib.pyplot as plt

# open binary files
image_names = ["test{}_out.bin".format(i) for i in range(0, 5)]

# reshape to 512, 768
images = [np.fromfile(x, dtype=np.uint16).reshape(512, 768) for x in image_names]

# plot images in seperate figures
for i in range(0, 5):
    plt.figure()
    plt.imshow(images[i], cmap='gray')
    plt.show()