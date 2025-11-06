#include

int pipe_I_B[2];
int pipe_D_B[2]; 
int pipe_B_D[2];

pid_t pid_B, pid_D, pid_I;

int main(){

    printf("Creazione dei canali di comunicazione (Pipe)...\n");
    if (pipe(pipe_I_B) == -1 || pipe(pipe_D_B) == -1 || pipe(pipe_B_D) == -1) {
        perror("Errore nella creazione della pipe");
        exit(EXIT_FAILURE);
    }

    if((pid_B = fork() ) ==0 ){
        close(pipe_I_B[1]);
        close(pipe_D_B[1]);
        close(pipe_B_D[0]);

    }

    if((pid_D = fork() ) ==0 ){
        close(pipe_I_B[0]);
        close(pipe_I_B[1]);
        close(pipe_D_B[0]);
        close(pipe_B_D[1]);
        
    }

    if((pid_I = fork() ) ==0 ){
        close(pipe_D_B[0]);
        close(pipe_D_B[1]);
        close(pipe_B_D[0]);
        close(pipe_B_D[1]);
        close(pipe_I_B[0]);


    }

   

    
    // Blocco del Processo Padre (continua dopo tutti i fork)
    if (pid_B > 0 && pid_D > 0 && pid_I > 0) {
        // Il Padre chiude TUTTE le pipe
        close(pipe_I_B[0]); 
        close(pipe_I_B[1]); 
        close(pipe_D_B[0]); 
        close(pipe_D_B[1]); 
        close(pipe_B_D[0]);
        close(pipe_B_D[1]);

        wait(pid_B);
        wait(pid_D);
        wait(pid_I);
        
        return 0;
    }



    

}