#include<iostream>

int main() {
    int n;
    int num = 1;
    std::cin>> n;
    while (num <= n ) {
        if (num % n == 0) {
            num++;
            std::cout << "Prime!" << std::endl;
        }else if (n % 3 && 5) {
            std::cout << "FizzBuzz" << std::endl; 
        }else if (n % 3) {
            std::cout << "Fizz" << std::endl;
        }else if (n % 5) {
            std::cout << "Buzz" << std::endl;
        }else {
           std::cout << n << std::endl; 
        }
    }
}