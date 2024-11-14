/** 
 * @author Burton O Sumner 
 * */
import javax.swing.*;
import javax.swing.text.SimpleAttributeSet;
import javax.swing.text.StyleConstants;

import java.io.*;
import java.net.*;
import java.util.HashMap;
import java.util.LinkedList;
import java.util.Queue;
import java.awt.*;

public class GroupChatServer extends JFrame {
    // Map of clients connected to server. Maps the client's respective handler thread according to their username.
    private HashMap<String, ClientHandle> clients = new HashMap<String, ClientHandle>();
    // Queue of messages to dispatch to all clients. ClientHandle instances add messages, read in from
    // client socket, to the queue and the main thread spends while loop duration sending the messages
    // to all clients via their respective socket printwriter.
    private Queue<ServerMessage> outbox = new LinkedList<ServerMessage>();
    
    // Text panel window to display status of the server (connections, disconnections, and messages)
    private JTextPane logs;
    // Navigable list of users.
    private JList<String> userList;

    // Nested class instance, ListeningThread, which runs as its own thread and handles incoming
    // client connection requests, dispatching new connections to their own ClientHandle thread.
    private ListeningThread listener;

    // Used by loops as a signal for whether or not to break from loop.
    private boolean disposeCalled = false;
    
    // Simple struct-type class that essentially tuples together the sending client via their
    // respective client handle thread, of type ClientHandle, and the actual contents of the
    // message contents itself, already nicely formatted as <[header field]>\t<arg>\t<arg>\t...
    // when parameterized in constructor calls
    private class ServerMessage {
        ClientHandle sender;
        String contents;
        public ServerMessage(ClientHandle sender, String contents) {
            this.sender = sender;
            this.contents = contents;
        }
    }
    // Add client to the list of connected clients and then dispatch a message to the queue alerting
    // all clients of the newly-connected user.
    public void addClient(ClientHandle client) {
        synchronized (clients) {
            if (clients.containsKey(client.username)) {
                int num = 1;
                String newUname = client.username.concat(Integer.toString(num));
                // Resolve username collision by appending number to the client-supplied username,
                // increments number appended until a username that is available is found.
                while (clients.containsKey(newUname)) {
                    num++;
                    newUname = client.username.concat(Integer.toString(num));
                }
                // Set client handle's instance variable, userName to the new username after its change.
                client.username = newUname;
            }
            clients.put(client.username, client);
        }
        // Reset userList JList GUI component's list data to include the new user.
        userList.setListData(clients.keySet().toArray(new String[clients.keySet().size()]));
        // Queue in a message to be sent to all clients alerting them of a new connection in order to
        // add their username to their lists of connected users.
        enqueueMessage(new ServerMessage(client, 
                "[NC]\t".concat(client.username)), false);
        logAppend(client.username, Color.BLUE, true);
        logAppend(" has connected from ", null, false);
        logAppend(String.format("%s:%d\n", 
                client.sockAddr.getAddress().toString(), 
                client.sockAddr.getPort()), Color.CYAN, true);
    }
    

    // Essentially a way to add text to the JTextPane in such a way that we can
    // set the text to be formatted a different color and/or bold/regular.
    public void logAppend(String text, Color color, boolean bold) {
        SimpleAttributeSet attr = new SimpleAttributeSet();
        StyleConstants.setBold(attr, bold);
        StyleConstants.setForeground(attr, (color==null ? Color.BLACK : color));
        


        logs.setEditable(true);
        logs.setCaretPosition(logs.getDocument().getLength());
        logs.setCharacterAttributes(attr, false);
        logs.replaceSelection(text);
        logs.setEditable(false);
    }
    

    
    // Remove client from clients hashmap and then alert all still-connected clients of
    // the disconnection.
    public void removeClient(ClientHandle client) {
        synchronized (clients) {
            clients.remove(client.username, client);
        }
        enqueueMessage(new ServerMessage(client, 
                "[DC]\t".concat(client.username)), false);
        userList.setListData(clients.keySet().toArray(new String[clients.keySet().size()]));
        if (disposeCalled)
            return;
        logAppend(client.username, Color.BLUE, true);
        logAppend(" has disconnected.\n", null, false);
    }

    public void enqueueMessage(ServerMessage msg, boolean echoToLog) {
        outbox.add(msg);
        if (!echoToLog)
            return;
        // else: must be a client chat message.
        logAppend(msg.sender.username + ": ", Color.BLUE, true);
        logAppend(msg.contents.split("\t")[2] + "\n", null, false);
    }

    /** 
     * {@summary Listening socket handler thread.}
     * A Thread subclass that handles incoming connection requests via a listening 
     * socket ({@link ServerSocket}) */
    private class ListeningThread extends Thread {
        ServerSocket listenerSocket;
        boolean shouldExit;

        
        public ListeningThread(int portNumber) throws IOException {
            super();
            listenerSocket = new ServerSocket(portNumber);
            shouldExit = false;
        }

        public void exit() {
            shouldExit = true;
            try { 
                listenerSocket.close(); 
            } catch (IOException e) { 
                e.printStackTrace(); 
            }
            interrupt();
        }
        
        @Override
        public void run() {
            while (!shouldExit && listenerSocket.isBound()) {
                try {
                    Socket sock = listenerSocket.accept();
                    if (sock == null || !sock.isConnected() || sock.isClosed())
                        continue;
                    ClientHandle client = new ClientHandle(sock);
                    client.start();
                } catch (IOException e) {
                    logAppend(e.getMessage(), null, true);
                    continue;
                }
            }
            try {
                listenerSocket.close();
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
    }


    /** 
     * {@summary Thread subtype that essentially establishes the connection by exchanging first-pass details 
     * and then acts as a socket input reader using {@link BufferedReader} that reads from client socket.}*/
    private class ClientHandle extends Thread {
        String username;
        InetSocketAddress sockAddr;
        BufferedReader sockIn;
        PrintWriter sockOut;
        Socket socket;
        boolean shouldExit = false;

        public ClientHandle(Socket socket) {
            this.socket = socket;
        }

        public void sendMessage(ServerMessage msg) {
            if (msg.sender.equals(this))
                return;
            sockOut.println(msg.contents);
        }
        
        public boolean equals(ClientHandle other) {
            return other.username.equals(username) && other.socket.equals(socket);
        }

        public void exit() {
            shouldExit = true;
            try {
                socket.close();
                sockIn.close();
                sockOut.close();
            } catch (IOException e) {
              e.printStackTrace();
            }
            interrupt();
        }
        @Override
        public void run() {
            try {
                sockOut = new PrintWriter(socket.getOutputStream(), true);
                sockIn = new BufferedReader(new InputStreamReader(socket.getInputStream()));
                String s = sockIn.readLine();
                if (s == null) {
                    return;
                }
                String[] split = s.split("\t");
                if (split==null || split.length!=2 || !split[0].equals("[UNAME]")) {
                    System.err.println("\033[1;31m[Client Connection Error]:\033[0m " + 
                            "Malformed data from client during connection handshake.");
                    return;
                }
                username = split[1];
                sockAddr = ((InetSocketAddress) (socket.getRemoteSocketAddress()));
            } catch (IOException e) {
                e.printStackTrace();
                return;
            }
            addClient(this);
            // Acknowledgement message signals to client that the client has been officially added to the server
            // and also signals their new username if it had to be modified to satisfy uniqueness requirement,
            // and also contains the name of all users connected to the server as tab-delimited args in the ACK_CONN
            // message to be sent to the client.
            // Fmt: [ACK_CONN]\t<their registered username>\t<connected client username>\t<connected client username>\t<...>
            String acknowledgementMsg = "[ACK_CONN]\t".concat(username);
            synchronized (clients) {
                java.util.Collection<ClientHandle> clientHandles = clients.values();
                for (ClientHandle handle : clientHandles) {
                    acknowledgementMsg = String.format("%s\t%s", acknowledgementMsg, handle.username);
                }
            }
            sockOut.println(acknowledgementMsg);
            try {
                while (!shouldExit) {
                    String msg = sockIn.readLine();
                    if (msg == null) {
                        break;
                    }
                    msg = msg.replaceAll("\t", " "); 
                    enqueueMessage(new ServerMessage(this, 
                            String.format("[MSG]\t%s\t%s", username, msg)), true);
                    continue;       
                }
                socket.close();
                sockOut.close();
                sockIn.close();
            } catch (IOException e) {
                System.out.println("[Client: " + username + "]: " + e.getMessage());
            }
            removeClient(this);
        }
    }

    // Establishes the GUI and the listening socket, as well as pretty much all primary object instances.
    public GroupChatServer(int portNo) {
        super(String.format("Group Chat Server Hosted On Port %d", portNo));
        setSize(1280, 720);
        setDefaultCloseOperation(JFrame.DISPOSE_ON_CLOSE);
        Container root = getContentPane();
        root.setLayout(new BoxLayout(root, BoxLayout.PAGE_AXIS));
        userList = new JList<String>();
        logs = new JTextPane();
        logs.setEditable(false);
        JScrollPane uListWrapper = new JScrollPane(userList),
                    logsWrapper = new JScrollPane(logs);
        logsWrapper.setHorizontalScrollBarPolicy(
            JScrollPane.HORIZONTAL_SCROLLBAR_NEVER);

        logsWrapper.setVerticalScrollBarPolicy(
            JScrollPane.VERTICAL_SCROLLBAR_AS_NEEDED);

        uListWrapper.setHorizontalScrollBarPolicy(
            JScrollPane.HORIZONTAL_SCROLLBAR_NEVER);

        uListWrapper.setVerticalScrollBarPolicy(
            JScrollPane.VERTICAL_SCROLLBAR_AS_NEEDED);

        JSplitPane splitView = new JSplitPane(JSplitPane.VERTICAL_SPLIT, uListWrapper, logsWrapper);
        root.add(splitView);
        try {
            listener = new ListeningThread(portNo);
        } catch (IOException e) {
            e.printStackTrace();
            System.exit(1);
        }
        listener.start();
        setVisible(true);
    }

    // Override dispose method from superclass, JFrame, in order to make it close all connections cleanly
    // and then interrupt the listening thread and all client handle threads before exiting. That way all
    // sockets are closed cleanly, all before finally calling the superclass's dispose method implementation
    // in order to close and clean up all of the GUI stuff.
    @Override
    public void dispose() {
        disposeCalled = true;
        listener.exit();
        synchronized (clients) {
            for (ClientHandle handle : clients.values()) {
                handle.exit();
            }
        }
        super.dispose();
    }

    public static void main(String[] args) {
        int port = 0;
        if (args.length != 1) {
            System.err.println("\033[1;31m[Error]:\033[0m Invalid arg count." + 
                    "\033[1;34m[Usage]:\033[0m java GroupChatServer <port number>");
        }
        try {
            port = Integer.parseInt(args[0]);
        } catch (NumberFormatException e) {
            System.err.println("Invalid port number arg: " + e.getMessage());
            System.exit(1);
        }
        GroupChatServer server = new GroupChatServer(port);
        
        while (!server.disposeCalled) {
            if (server.outbox.peek()==null) try {
                Thread.sleep(16);
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
            // else: Messages to send to all connected clients are in queue. So,
            // dequeue them and then send them to all clients except the actual
            // initial sender client that is specified in the ServerMessage
            // dequeued.
            ServerMessage msg = server.outbox.poll();
            if (msg==null) continue;
            synchronized (server.clients) {
                for (ClientHandle handle : server.clients.values()) {
                    handle.sendMessage(msg);
                }
            }
        }
    }
}

