# EventBroadcaster plugin
A plugin that sends all events via a network socket using the [0MQ library](http://zeromq.org/).

## Installation
### Installing the 0MQ library
For windows, linux, and mac, the required files are already included for the plugin

### Building the plugins
Building the plugins requires [CMake](https://cmake.org/). Detailed instructions on how to build open ephys plugins with CMake can be found in [our wiki](https://open-ephys.atlassian.net/wiki/spaces/OEW/pages/1259110401/Plugin+CMake+Builds).

#### [MacOS only] Update rpaths
Update the rpaths of HDF5 libraries linked to `EventBroadcaster.bundle` by running the following commands:
```
install_name_tool -change /usr/local/lib/libzmq.5.dylib @rpath/libzmq.5.dylib path/to/EventBroadcaster.bundle/Contents/MacOS/EventBroadcaster

```