#include <string.h>

class container_object;

#define MAX_BUFFER_SIZE 64

class target
{
    public:
        target(){ memset(m_name, '\0', MAX_BUFFER_SIZE); }
        void populate(container_object* obj);
        const char* get_name(){ return m_name; }
        virtual void populate_specific(container_object* obj) = 0;

    private:
        char m_name[MAX_BUFFER_SIZE];
};

class set_target: public target
{
    public:
        set_target()
        {
            memset(m_ip, '\0', MAX_BUFFER_SIZE);
            memset(m_model, '\0', MAX_BUFFER_SIZE);
        }
        void populate_specific(container_object* obj);
        const char* get_ip(){ return m_ip; }
        const char* get_model(){ return m_model; }

    private:
        char m_ip[MAX_BUFFER_SIZE];
        char m_model[MAX_BUFFER_SIZE];
};

void parse(const char* json, target* t);
