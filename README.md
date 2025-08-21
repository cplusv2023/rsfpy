# rsfpy

**rsfpy** is a Python toolkit for reading, writing, and manipulating [*Madagascar*](https://ahay.org "Madagascar Main Page") [<ins>*RSF* (Regularly Sampled Format) </ins>](https://ahay.org/wiki/Guide_to_RSF_file_format "RSF data format")  scientific datasets.  
Built on top of [*NumPy*](https://numpy.org/ "The fundamental package for scientific computing with Python"), it supports efficient slicing, transposing, and subsampling, while automatically updating metadata (e.g., n#/o#/d#/label#/unit#) to keep axis descriptions consistent with data transformations.

## ✨ Features
Below fancy descriptions are AI-generated. Actually you can use this package for data io ONLY (for now. I am planning to add more, but it takes time).
- 📂 **Data I/O**: Read and write RSF format files
- ⚡ **High performance**: NumPy-based for large-scale data processing
- 🔄 **Metadata sync**: Automatically updates axis information during operations
- 🛠 **Clean API**: Intuitive interface for research workflows
- 🌍 **Applications**: Geophysics, signal processing, seismic imaging, and more

## 📦 Installation

```bash
pip install .
```

### 🚀 Quick start 

```python
# import 
import io
import numpy as np
from rsfpy.array import Rsfarray

# Read from RSF file (file path)
rarray = Rsfarray("./data.test")

# Read from fileio
with open("./test/data.test") as fp:
    rarray = Rsfarray(fp)

# Initialize from ndarray
narray = np.array([1,2,3])
rarray = Rsfarray(narray, 
                  header={'d1':1,'o1':0,'label1':'X', 'unit1':''},
                  history="Ndarray [1,2,3]"
                  )
# Empty array
empty = Rsfarray()
# Override array
empty.read("./data.test")

# Write to file
rarray.write("./saved.test", # header file
             out='stdin', # data path, 'stdin' or None: append header file
             form='ascii', # 'native' for binary 
             fmt='%.4e', # The data format for ascii (default is "%f").
             )

# Write to io stuff
meta = io.StringIO()
dat = io.BytesIO()
rarray.write(meta, out=dat)

# Use Rsfarray properties
## Axis
data = Rsfarray("./data.test")
taxis, xaxis = data.axis1, data.axis2
### Or
taxis, xaixs = data.axis([0,1])
## Sampling parameters
nt, dt, ot, label1, unit1 = data.n1, data.d1, data.o1, data.label1, data.unit1
nx, dx, ox, label2, unit2 = data.n2, data,d2, data.o2, data.label2, data.unit2
### Or
nt, nx = data.n([0,1])
dt, dx = data.d([0,1])
### ...

## Try transpose
print(f"before transpose: {data.label1, data.label2}")
data = data.T
print(f"after transpose: {data.label1, data.label2}")

# Window data use .window to sync metadata
print(f"before window: {data.d1, data.o1}")
data1 = data.window("j1=5 f1=100")
## Or
data1 = data.window(j1=5, f1=100)
print(f"after window: {data1.d1, data1.o1}")

```


### 📚 Requirements
- Python >= 3
- Numpy
### 📄 License
GNU GPLv2
### 🤝 Contributing
Contributions are welcome! Feel free to open an issue or submit a pull request to improve **rsfpy**

