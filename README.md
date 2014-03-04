# Ubuntu pre-requirements
apt-get install make g++ scons libboost1.48-all-dev libcurl4-openssl-dev libgraphicsmagick++1-dev

wget http://downloads.mongodb.org/cxx-driver/mongodb-linux-x86_64-v2.2-latest.tgz -O mongodb.tgz
tar xvzf mongodb.tgz
cd mongo-cxx-driver*
scons install
cd ..
rm -Rf mongodb.tgz mongo-cxx-driver*

wget http://download.zeromq.org/zeromq-3.2.2.tar.gz -O zeromq.tgz;
tar xvzf zeromq.tgz;
cd zeromq-*;
./configure;
make install;
ldconfig;cd ..;
rm -Rf zeromq.tgz zeromq-*

[![Bitdeli Badge](https://d2weczhvl823v0.cloudfront.net/konteck/grider/trend.png)](https://bitdeli.com/free "Bitdeli Badge")

