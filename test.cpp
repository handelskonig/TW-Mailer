#include <iostream>
#include <sstream>
#include <chrono>

int main(){
    using namespace std::chrono;
    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    std::string time = std::ctime(&timeT);

    time.pop_back();
    time.pop_back();
    //time.erase(time.back());
    std::cout << time; 

    return 0;
}