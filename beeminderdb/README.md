# BeeMinder

## Install
Before using this package you need to install it with the following command 
from the `beeminderdb` directory:
```
python -m pip install --user -e ./
```

This command will take care of installing the `beeminder` package and its 
dependencies.If there is a problem install instructions for `beeminder`'s 
dependencies can be found below. 

### Manually Installing Dependencies
This package has a dependency of `pymongo`. To install pymongo run the 
following command:
```
python pip install pymongo[tls]
```

Aditionally `dnspython` must be installed for this package to work properly. 
Use the following command:
```
python -m pip install dnspython
```

NOTE: other dependencides will need to be added, I will fix this later
## Usage
This package contains the BeeMinder class that is used to send data to the 
BeeMinder backed by our base station.

To use this package you will need to add the colloring import to your python 
module
```
include beeminder
```


