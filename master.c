#include <fcntl.h> /* For O_* constants */
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h> /* For mode constants */
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define CANT_PROCESS 3
#define INITIAL_FILES 3
#define MAX_FILES 100

#define READ 0
#define WRITE 1

typedef struct buffer {
  char arr[256 * 6];
} buffer;

int main(int argc, char* argv[]) {
  int cant_cnf_asig, i, c;
  int cant_cnf_unsol = argc - 1;

  if (argc <= 1) {
    printf("Error, debe pasar unicamente el path de la carpeta contenedora de los archivos\n");
    exit(1);
  }

  const char* shm_name = "/buffer";  // file name
  /* create the shared memory segment as if it was a file */
  int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
  if (shm_fd == -1) {
    perror("shm_open: No se pudo abrir la shared memory");
    exit(EXIT_FAILURE);
  }

  /* map the shared memory segment to the address space of the process */
  buffer* shm_ptr = mmap(0, sizeof(buffer) * MAX_FILES, PROT_READ, MAP_SHARED, shm_fd, 0);
  if (shm_ptr == MAP_FAILED) {
    perror("mmap: No se pudo mapear la shared memory");
    close(shm_fd);
    shm_unlink(shm_name);
    exit(EXIT_FAILURE);
  }

  /*Variables for pipes*/
  int pipeMW[CANT_PROCESS][2];  //pipeMW - Master Writes
  int pipeMR[CANT_PROCESS][2];  //pipeMR - Master Reads
  pid_t cpid[CANT_PROCESS] = {100};
  int solved_queries[CANT_PROCESS];

  /*Variables for select*/
  fd_set readfds, writefds;
  FD_ZERO(&readfds);
  FD_ZERO(&writefds);
  int max_fd = 0;
  //fflush(stdout);

  for (i = 0; i < CANT_PROCESS; i++) {
    pipe(pipeMW[i]);
    pipe(pipeMR[i]);
    cpid[i] = fork();
    if (cpid[i] == -1) {
      perror("fork");
      exit(EXIT_FAILURE);
    }
    if (cpid[i] == 0) {
      close(pipeMW[i][WRITE]);
      close(pipeMR[i][READ]);
      dup2(pipeMW[i][READ], STDIN_FILENO);    //El esclavo le llega por estandar input lo que escriben en el FD
      dup2(pipeMR[i][WRITE], STDOUT_FILENO);  //Salida estandar del esclavo se redirecciona al FD
      char* args[] = {"./slave", NULL};
      execvp(args[0], args);
      perror("exec");
      exit(EXIT_FAILURE);
    } else {
      close(pipeMW[i][READ]);
      close(pipeMR[i][WRITE]);
      if (pipeMR[i][READ] > max_fd)
        max_fd = pipeMR[i][READ];
      FD_SET(pipeMR[i][READ], &readfds);
    }
  }

  //Asignacion de INITIAL_FILES queries a cada esclavo

  for (i = 0, cant_cnf_asig = 0; i < CANT_PROCESS && cant_cnf_asig < argc - 1; i++)
    for (c = 0; c < INITIAL_FILES && cant_cnf_asig < argc - 1; c++, cant_cnf_asig++) {
      char aux[100] = {};
      strcpy(aux, argv[cant_cnf_asig + 1]);
      strcat(aux, "\n");
      write(pipeMW[i][WRITE], aux, strlen(aux));
    }

  int k;
  char line[100];
  size_t linecap = 0;
  ssize_t linelen;
  int node_id = 0;
  while (cant_cnf_unsol > 0) {
    select(max_fd + 1, &readfds, NULL, NULL, NULL);

    for (k = 0; k < CANT_PROCESS && cant_cnf_unsol > 0; k++) {  //unsolve = argc - 1 - asignated
      if (FD_ISSET(pipeMR[k][READ], &readfds)) {
        linelen = read(pipeMR[k][READ], &line, 256);
        strncpy((shm_ptr + node_id++)->arr, line, strlen(line));
        //printf("%s\n", line);
        //Donde mandamos lo leido?
        cant_cnf_unsol--;
        solved_queries[k]++;
        if (solved_queries[k] >= INITIAL_FILES && cant_cnf_asig < argc - 1) {
          char aux[100] = {};
          strcpy(aux, argv[cant_cnf_asig + 1]);
          strcat(aux, "\n");
          printf("%s este\n", aux);
          if (write(pipeMW[i][WRITE], aux, strlen(aux)) == -1) {
            printf("Error write\n");
            exit(EXIT_FAILURE);
          }
          cant_cnf_asig++;
        }
      }
    }
    for (i = 0; i < CANT_PROCESS; i++)
      FD_SET(pipeMR[i][READ], &readfds);
  }

  int status;
  for (i = 0; i < CANT_PROCESS; i++) {
    close(pipeMW[i][WRITE]);
    close(pipeMR[i][READ]);
    //waitpid(cpid[i], &status, 0);
  }

  /* remove the mapped shared memory segment from the address space of the process */
  if (munmap(shm_ptr, sizeof(buffer) * MAX_FILES) == -1) {
    perror("munmap: No se pudo desmapear la shared memory");
    close(shm_fd);
    shm_unlink(shm_name);
    exit(EXIT_FAILURE);
  }

  /* close the shared memory segment as if it was a file */
  if (close(shm_fd) == -1) {
    perror("close: No se pudo cerrar el FD de la SM");
    shm_unlink(shm_name);
    exit(EXIT_FAILURE);
  }
  // remove the shared memory segment from the file system
  if (shm_unlink(shm_name) == -1) {
    perror("shm_unlink: No se pudo cerrar la shared memory");
    exit(EXIT_FAILURE);
  }
  return 0;
}