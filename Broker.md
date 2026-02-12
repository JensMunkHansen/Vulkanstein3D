es, it's a broker/mediator pattern. The graph sits between producers (SceneManager provides textures, renderer provides images) and consumers (stages), handling allocation, sharing, and lifetime. Stages stay 
  decoupled from each other — they only talk to the graph.
                                                                                                                                                                                                                   
  It also means adding a new stage (like the thickness pre-raycast) is just: declare bindings, register with graph, done. No wiring through app.cpp, no manual update_descriptor_layout() calls, no worrying about 
  what other stages exist. 
