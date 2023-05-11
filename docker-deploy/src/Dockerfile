FROM gcc:latest
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update
RUN apt-get install -y --no-install-recommends libboost-all-dev
RUN mkdir /code
WORKDIR /code
ADD . /code/
RUN mkdir /var/log/erss
CMD ["./run.sh"]




