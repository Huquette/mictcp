#include <mictcp.h>
#include <../include/api/mictcp_core.h> 

#define PERTES_ADM 0
#define TIMER 10

// Variables globales :
mic_tcp_sock sock;
mic_tcp_sock_addr addr_socket_dest;
int PE = 0;
int PA = 0;
int nb_envoyes = 0;
int numero_paquet = 0;

/*
 * Permet de créer un socket entre l’application et MIC-TCP
 * Retourne le descripteur du socket ou bien -1 en cas d'erreur
 */
int mic_tcp_socket(start_mode sm)
{
	int result = -1;
	printf("[MIC-TCP] Appel de la fonction: ");  
	printf(__FUNCTION__); 
	printf("\n");
   
	/* Appel obligatoire */   
	if ((result = initialize_components(sm)) == -1){ 
        perror("erreur de création de socket\n");
		return -1;
	}
	else {
		// Descripteur du socket
		sock.fd = 0;
		// Etat du socket
		sock.state = IDLE;
		// Permet de réguler le pourcentage de pertes admissibles
		set_loss_rate(PERTES_ADM);
		return sock.fd;
	}
}

/*
 * Permet d’attribuer une adresse à un socket.
 * Retourne 0 si succès, et -1 en cas d’échec
 */
int mic_tcp_bind(int socket, mic_tcp_sock_addr addr)
{
   printf("[MIC-TCP] Appel de la fonction: ");
   printf(__FUNCTION__); 
   printf("\n");
   
   // Il faut vérifier que c'est le bon socket
   if (sock.fd == socket){
	   // memcpy car c'est une copie d'une structure à une autre
	   memcpy((char*)&sock.addr,(char*)&addr, sizeof(mic_tcp_sock_addr));
	   return 0;
   }
   else {
	   return -1;
   }
}

/*
 * Met le socket en état d'acceptation de connexions
 * Retourne 0 si succès, -1 si erreur
 */
int mic_tcp_accept(int socket, mic_tcp_sock_addr* addr)
{
    printf("[MIC-TCP] Appel de la fonction: ");  
	printf(__FUNCTION__); 
	printf("\n");
	
	// Il faut vérifier que c'est le bon socket et que la connexion n'est pas fermée
	if ((sock.fd == socket) && (sock.state != CLOSED)){
		// Connexion établie
		sock.state = ESTABLISHED; 
		return 0;
	}
	else {
		return -1;
	}
}

/*
 * Permet de réclamer l’établissement d’une connexion
 * Retourne 0 si la connexion est établie, et -1 en cas d’échec
 */
int mic_tcp_connect(int socket, mic_tcp_sock_addr addr)
{
    printf("[MIC-TCP] Appel de la fonction: ");  
	printf(__FUNCTION__); 
	printf("\n");
	
	// Il faut vérifier que c'est le bon socket et que la connexion n'est pas fermée
	if ((sock.fd == socket) && (sock.state != CLOSED)){
		// Connexion établie
		sock.state = ESTABLISHED; 
		// L'adresse de destination est addr
		addr_socket_dest = addr;
		return 0;
	}
	else {
		return -1;
	}
}

/*
 * Permet de réclamer l’envoi d’une donnée applicative
 * Retourne la taille des données envoyées, et -1 en cas d'erreur
 */
int mic_tcp_send (int mic_sock, char* mesg, int mesg_size)
{
    printf("[MIC-TCP] Appel de la fonction: "); 
	printf(__FUNCTION__); 
	printf("\n");
	
	mic_tcp_pdu PDU;
	int sent_size;
	
	if ((sock.fd == mic_sock) && (sock.state == ESTABLISHED)){
		// On construit le PDU : header et payload
		PDU.header.source_port = sock.addr.port;
		PDU.header.dest_port   = addr_socket_dest.port;
        PDU.header.seq_num     = PE;
		PDU.header.syn         = 0;
		PDU.header.ack		   = 0;
		PDU.header.fin		   = 0;
		
		PDU.payload.data 	   = mesg;
		PDU.payload.size	   = mesg_size;

		PE = (PE+1)%2;

        int fin = 0;
        while(!fin) {
            sent_size = IP_send(PDU, addr_socket_dest);
			printf("Envoi du paquet : %d, tentative n° : %d.\n",numero_paquet,nb_envoyes);
			numero_paquet++;
        	nb_envoyes++;
            sock.state = WAITING_FOR_ACK;
            // Activation du timer:
            if (IP_recv(&PDU, &addr_socket_dest, TIMER) == -1) {
                // On renvoie le PDU si le timer a expiré
                sent_size = IP_send(PDU, addr_socket_dest);
				printf("Renvoi du paquet : %d, tentative n° : %d.\n",numero_paquet,nb_envoyes);
				nb_envoyes++;
            }
            // Si le timer n'a pas expiré
            else {
                // On sort du while seulement si on a le bon ack
                if (PDU.header.ack && PDU.header.ack_num == PE) {
                    fin = 1;
                }
            }
        }

        sock.state = ESTABLISHED;
		return sent_size;
	} 
	else {
		return -1;
	}
}

/*
 * Permet à l’application réceptrice de réclamer la récupération d’une donnée
 * stockée dans les buffers de réception du socket
 * Retourne le nombre d’octets lu ou bien -1 en cas d’erreur
 * NB : cette fonction fait appel à la fonction app_buffer_get()
 */
int mic_tcp_recv (int socket, char* mesg, int max_mesg_size)
{
    printf("[MIC-TCP] Appel de la fonction: "); 
	printf(__FUNCTION__); 
	printf("\n");
	
	mic_tcp_payload PDU;
	int nbr_octets_lus;
	
	// Payload
	PDU.data = mesg;
	PDU.size = max_mesg_size;
	
	if ((sock.fd == socket) && (sock.state == ESTABLISHED)){
		// Mise en attente d'un PDU par l'état IDLE
		sock.state = IDLE;
		
		// Retrait d'un PDU dans le buffer de réception
		nbr_octets_lus = app_buffer_get(PDU);
		
		// Etat connecté suite à la récupération du PDU
		sock.state = ESTABLISHED;
		
		return nbr_octets_lus;
	}
	else {
		return -1;
	}
}

/*
 * Permet de réclamer la destruction d’un socket.
 * Engendre la fermeture de la connexion suivant le modèle de TCP.
 * Retourne 0 si tout se passe bien et -1 en cas d'erreur
 */
int mic_tcp_close (int socket)
{
    printf("[MIC-TCP] Appel de la fonction :  "); 
	printf(__FUNCTION__); 
	printf("\n");
	
	// Si c'est le bon socket et que la connexion est en cours
	if ((sock.fd == socket) && (sock.state == ESTABLISHED)){
		// Se mettre en état de connexion fermée
		sock.state = CLOSED;
		return 0;
	}
	else {
		return -1;
	}
}

/*
 * Traitement d’un PDU MIC-TCP reçu (mise à jour des numéros de séquence
 * et d'acquittement, etc.) puis insère les données utiles du PDU dans
 * le buffer de réception du socket. Cette fonction utilise la fonction
 * app_buffer_put().
 */
void process_received_PDU(mic_tcp_pdu pdu, mic_tcp_sock_addr addr)
{
    printf("[MIC-TCP] Appel de la fonction: "); 
	printf(__FUNCTION__); 
	printf("\n");

	// Header de notre PDU
	pdu.header.ack_num     = PA;
	pdu.header.ack         = 1;

	
    IP_send(pdu, addr);

	if (pdu.header.seq_num == PA){
        // Insertion des données utiles (message + taille) du PDU dans le buffer de réception du socket
        app_buffer_put(pdu.payload);
		sock.state = ESTABLISHED;
		// On ne peut envoyer/recevoir qu'un message à la fois
        PA = (PA+1) %2;
    }
}
