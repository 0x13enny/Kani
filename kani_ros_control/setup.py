from distutils.core import setup
from catkin_pkg.python_setup import generate_distutils_setup

# fetch values from package.xml
setup_args = generate_distutils_setup(
    packages = ['kani_ros_control'],
    package_dis = {'': 'src'},
    install_requires=[]
)
setup(**setup_args)
