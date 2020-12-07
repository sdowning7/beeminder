from setuptools import setup, find_packages

setup(
    name='BeeMinder', 
    version='1.0', 
    packages=find_packages(),
    install_requires=[
        'pymongo[tls]',
        'dnspython'
      ])
