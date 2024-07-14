// Mutual exclusion lock.
struct spinlock {
  uint locked;       //0: lock available, non-0: lock held
  
  // For debugging:
  char *name;        // Name of lock.
  struct cpu *cpu;   // The cpu holding the lock.
};

