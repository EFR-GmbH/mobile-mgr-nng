# Mobile Manager NNG

Mobile Manager NNG is a project that provides a server to handle MBIM and QMI requests using NNG for communication.

## Features
- Handle MBIM and QMI requests
- Asynchronous operations
- NNG-based server for request handling

## Build Instructions

1. Clone the repository:
    ```sh
    git clone https://github.com/your-repo/mobile-mgr-nng.git
    cd mobile-mgr-nng
    ```

2. Create a build directory and navigate to it:
    ```sh
    mkdir build
    cd build
    ```

3. Run CMake to configure the project:
    ```sh
    cmake ..
    ```

4. Build the project:
    ```sh
    make
    ```

5. Optionally, build the sample client:
    ```sh
    cmake -DSAMPLE_CLIENT=ON ..
    make
    ```

## Running the Server

To run the server, execute the following command from the build directory:
```sh
./mbim_nng
```

## NNG Interface

The NNG interface in this project uses a custom `databuf` structure for handling requests and responses.

### Example Usage
An example client is provided in the `sample/client.c` program.

## License

This project is licensed under the GNU General Public License v2.0. See the LICENSE file for details.
