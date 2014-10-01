**HAL (Heuristically programmed ALgorithmic computer) is a sentient computer 
(or artificial intelligence) that controls the systems of the UrLab spacecraft 
and interacts with the Hackerspace crew.**

The HAL project is an arduino based Human - Hackerspace interface, meant to be 
easy to use and reuse. It might as well be suitable for domotic applications. 
It consists of 3 main components:

* **hal-arduino**: The arduino library defining HAL behaviour
* **hal-driver** (this repo): An Fuse interface to control arduino
* **halpy**: A high-level Python library that automates a lot of boilerplate code

# HAL Fuse driver

## Dependencies

* fuse with development files (`apt-get install libfuse-dev` on debian)

## Compile

Simply `make`

## Run as foreground process

`./driver -f <mount point>`

## Allow other users to use the driver

Ensure that the line `user_allow_other` is present and not commented in 
`/etc/fuse.conf`.

Then, `./driver -o allow_other <mount point>`
