#include "proxy.hpp"

int main(int argc, char *argv[]){
    daemon(1,1);
    Proxy myproxy("12345");
    myproxy.run_service();   
     return EXIT_SUCCESS;
}