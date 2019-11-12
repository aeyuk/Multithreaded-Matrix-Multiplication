all: package compute

package: package.c
	gcc -Wall -std=gnu99 -pthread -o package package.c

compute: compute.c
	gcc -Wall -std=gnu99 -pthread -o compute compute.c
	
clean:
	rm *.0 encode
				
