from PIL import Image
import numpy as np
import matplotlib.pyplot as plt

image_list = ["test{}.png".format(i) for i in range(0, 5)]

# open and convert to grayscale
images = [Image.open(x).convert('L') for x in image_list]

# Show the images
for i in range(0, 5):
    plt.figure()
    plt.imshow(images[i], cmap='gray')
    plt.show()

# convert to numpy array of 16bit unsigned int
images = [np.array(x, dtype=np.uint16) for x in images]

# Print resolution of image
print (images[0].shape)

# flatten all images to 1D array
images = [x.flatten() for x in images]
images = np.array(images, dtype=np.uint16)

# Print size of image in bytes
print (images.nbytes)

# save as binary file with 16bit unsigned int
images.tofile("test.bin")