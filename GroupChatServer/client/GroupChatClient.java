/**
 * GroupChatClient
 * @author Burton O Sumner
 */

import javax.swing.*;
import javax.swing.text.SimpleAttributeSet;
import javax.swing.text.StyleConstants;

import java.awt.*;
import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.net.*;
import java.util.HashSet;
import java.io.*;

public class GroupChatClient extends JFrame {
    Socket socket;
    BufferedReader sockIn;
    PrintWriter sockOut;
    boolean disposeCalled;
    String userName, windowTitle;
    HashSet<String> userList = new HashSet<String>();

    /** {@summary Add text to the chat log text pane on the GUI.}
     * @param text Text to add. 
     * @param color The color the text should be displayed as. If null, defaults to black.
     * @param bold Boolean field, make text displayed as bold if true, otherwise regular. 
     * */
    private void logAppend(String text, Color color, boolean bold) {
        SimpleAttributeSet attr = new SimpleAttributeSet();
        StyleConstants.setBold(attr, bold);
        StyleConstants.setForeground(attr, (color==null ? Color.BLACK : color));

        logs.setEditable(true);
        logs.setCaretPosition(logs.getDocument().getLength());
        logs.setCharacterAttributes(attr, false);
        logs.replaceSelection(text);
        logs.setEditable(false);
    }
    
   /** 
    * {@summary Thread that reads from connection socket and relays to GUI.} 
    * Class that runs asyncronously by inheriting from {@link Thread}; reads from {@link BufferedReader} that is connected to 
    * {@link Socket} connecting client to group chat server. */
    private class SocketInputReceiver extends Thread {
        private boolean shouldExit;
        public SocketInputReceiver() {
            shouldExit = false;
        }
        @Override
        public void run() {
            try {
                while (!disposeCalled && !shouldExit) {
                    String msgRecvd = sockIn.readLine();
                    if (msgRecvd == null) {
                        System.out.println("Server disconnected.");
                        logAppend("[Disconnected from server]: ", Color.RED, true);
                        logAppend("Close window to exit program.", null, false);
                        break;
                    }
                    String[] split = msgRecvd.split("\t");
                    switch (split[0]) {
                        case "[NC]":
                            handleNewClientNotif(split);
                            continue;
                        case "[DC]":
                            handleDisconnectedClientNotif(split);
                            continue;
                        case "[MSG]":
                            handleMessageNotif(split);
                            continue;
                        default:
                            System.err.println("\033[1;31m[Error]:\033[0m Malformed data received." +
                                "raw message: " + msgRecvd);
                            continue;
                    }
                }
            } catch (IOException e) {
                System.out.println(e);
            }
            return;
        }


        private boolean handleMessageNotif(String[] splitMsg) {
            if (splitMsg.length != 3)
                return false;
            logAppend(splitMsg[1] + ": ", Color.BLUE, true);
            logAppend(splitMsg[2] + "\n", null, false);
            return true;

        }
        
        
        private boolean handleNewClientNotif(String[] splitMsg) {
            if (splitMsg.length != 2)
                return false;
            if (userList.contains(splitMsg[1]))
                return false;
            userList.add(splitMsg[1]);
            users.setListData(userList.toArray(new String[userList.size()]));
            logAppend(splitMsg[1], Color.BLUE, true);
            logAppend(" has connected.\n", null, false);
            return true;
        }

        private boolean handleDisconnectedClientNotif(String[] splitMsg) {
            if (splitMsg.length != 2)
                return false;
            if (!userList.contains(splitMsg[1]))
                return false;
            userList.remove(splitMsg[1]);
            users.setListData(userList.toArray(new String[userList.size()]));
            logAppend(splitMsg[1], Color.BLUE, true);
            logAppend(" has disconnected.\n", null, false);
            return true;
        }
    }
    // Handles messages received from server via a BufferedReader that reads from sockets inputstream.
    private SocketInputReceiver receiver;
    // Displays all server notifs and messages received.
    JTextPane logs;
    // Displays list of all users connected to the server
    JList<String> users;
    // Client message input text field.
    JTextField input;
    // Button to send client's inputed messages to server.
    JButton sendBtn;

    private void sendMessage(String msg) {
        msg = msg.replaceAll("\t", " ");
        sockOut.println(msg);
        logAppend(userName + ": ", Color.BLUE, true);
        logAppend(msg + "\n", null, false);
    }
    // Configure the GUI frontend to be formatted and displayed as neatly as I possibly could.
    private void configureUI() {
        setSize(1280, 720);
        setDefaultCloseOperation(JFrame.DISPOSE_ON_CLOSE);
        logs = new JTextPane();
        logs.setEditable(false);
        users = new JList<String>();
        Container root = getContentPane();
        root.setLayout(new BoxLayout(root, BoxLayout.Y_AXIS));
        JPanel inputPanel = new JPanel(new FlowLayout(FlowLayout.CENTER));
        input = new JTextField(64);
        sendBtn = new JButton("Send");
        sendBtn.addActionListener((ActionListener)((ActionEvent e) ->{
            String msg = input.getText();
            input.setText("");
            if (msg==null || msg.length()<=0)
                return;
            sendMessage(msg);
        }));
        inputPanel.add(input);
        inputPanel.add(sendBtn);
        JPanel splitViewWrapper = new JPanel(new BorderLayout(2,2));
        JScrollPane usersWrapper = new JScrollPane(users);
        usersWrapper.setVerticalScrollBarPolicy(JScrollPane.VERTICAL_SCROLLBAR_AS_NEEDED);
        usersWrapper.setHorizontalScrollBarPolicy(JScrollPane.HORIZONTAL_SCROLLBAR_NEVER);
        JScrollPane logsWrapper = new JScrollPane(logs);
        logsWrapper.setVerticalScrollBarPolicy(JScrollPane.VERTICAL_SCROLLBAR_AS_NEEDED);
        logsWrapper.setHorizontalScrollBarPolicy(JScrollPane.HORIZONTAL_SCROLLBAR_NEVER);
        JSplitPane splitView = new JSplitPane(JSplitPane.VERTICAL_SPLIT, usersWrapper, logsWrapper);
        splitViewWrapper.add(splitView);
        root.add(splitViewWrapper);
        root.add(inputPanel);
    }
    // Establishes connection to host of group chat server. Sends username, received uniqueness-resolved
    // username from server along with tab-delimited list of all connected users.
    private void connectToServer(String userName, String hostName, int portNumber) throws IOException {
        socket = new Socket(hostName, portNumber);
        sockIn = new BufferedReader(new InputStreamReader(socket.getInputStream()));
        sockOut = new PrintWriter(socket.getOutputStream(), true);
        sockOut.println("[UNAME]\t".concat(userName));
        String s = sockIn.readLine();
                
        if (s == null) {
            sockOut.close();
            sockIn.close();
            socket.close();
            throw new IOException("Disconnected during handshake.");
        }
        
        String[] split = s.split("\t");
        
        
        // Server connection acknowledgement reply message is tab delimited as:
        // <header>\t<username>\t<<tab-delim'd string of all connected client's username>>
        // Therefore split at the very least should contain:
        // [ACK_CONN]\t<this client's registered username>\t<this client's registered username>
        if (split.length < 3 || !split[0].equals("[ACK_CONN]")) {
            sockOut.close();
            sockIn.close();
            socket.close();
            throw new IOException("Malformed data received from server.");
        }
        this.userName = new String(split[1]);
        for (int i = 2; i < split.length; ++i) {
            userList.add(split[i]);
        }
        users.setListData(userList.toArray(new String[userList.size()]));
    }

    public GroupChatClient(String userName, String hostName, int portNumber) throws IOException {
        super();
        disposeCalled = false;
        configureUI();
        connectToServer(userName, hostName, portNumber);
        windowTitle = String.format("Group Chat | Username: %s | Server:  %s:%d", userName, hostName, portNumber);
        setTitle(windowTitle);
        receiver = new SocketInputReceiver();
        setVisible(true);
    }

    @Override
    public void dispose() {
        disposeCalled = true;
        receiver.shouldExit = true;

        try {
            socket.close();
            sockIn.close();
            sockOut.close();
        } catch (IOException e) {
            e.printStackTrace();
        }
        receiver.interrupt();

        super.dispose();
    }

    public static void main(String[] args) {
      if (args.length != 3) {
        System.err.println("\033[1;31m[Error]:\033[0m Invalid arg count.\n" +
            "\t\033[1;34m[Usage]:\033[0m " +
            "java GroupChatClient <username> <server name> <server port>");
        System.exit(1);
      }
        int port = 0;
        try {
            port = Integer.parseInt(args[2]);
        } catch (NumberFormatException e) {
             System.err.println(e);
             System.err.println("Invalid port number arg given. Could not parse"
                 + "port number from \"" + args[2] + "\"");
             System.exit(1);
        }
        GroupChatClient client = null;
        try {
            client = new GroupChatClient(args[0], args[1], port);
            client.receiver.start();
        } catch (IOException e) {
            System.out.println(e);
        }
    }
    
}
