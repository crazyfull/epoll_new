#ifndef LISTENER_H
#define LISTENER_H

class EpollReactor;
class Listener
{
public:
    Listener();
    ~Listener();

    bool Addlisten(int port);
private:
    EpollReactor* m_pReactor = nullptr;

};

#endif // LISTENER_H
