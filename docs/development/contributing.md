# Contributing to PostgreSQL Blockchain Extension

## Overview

Thank you for your interest in contributing to the PostgreSQL Blockchain Extension! This guide provides comprehensive information for developers who want to contribute to the project, whether through bug fixes, new features, documentation, or testing.

## Getting Started

### Prerequisites

Before contributing, ensure you have:

- **PostgreSQL Development Environment**: PostgreSQL 15+ with development headers
- **Build Tools**: GCC/Clang, Make, Git
- **Testing Environment**: Ability to create and destroy test databases
- **Understanding**: Basic knowledge of PostgreSQL internals and blockchain concepts

### Development Environment Setup

1. **Clone the Repository**:
```bash
git clone https://github.com/your-org/apostgres.git
cd apostgres
```

2. **Build the Extension**:
```bash
./configure --enable-debug --enable-cassert --prefix=/usr/local/pgsql
make && make install
```

3. **Initialize Test Database**:
```bash
initdb -D /usr/local/pgsql/data
pg_ctl start -D /usr/local/pgsql/data
createdb testdb
```

4. **Run Tests**:
```bash
cd apostgres
psql -d testdb -f blockchain_comprehensive_tests.sql
```

## Project Structure

### Core Components

```
src/backend/access/blockchain/
├── blockchainam.c          # Main table access method (3,630 lines)
├── blockchain_hash.c       # SHA-256 hash engine (1,065 lines)
├── blockchain_counter.c    # Global counter system (594 lines)
├── blockchain_utility.c    # DDL protection hooks (121 lines)
└── blockchain_anchor.c     # Anchor functionality (131 lines)

src/include/blockchain/
├── blockchainam.h          # TAM interface definitions
├── blockchain_hash.h       # Hash function interfaces
└── blockchain_counter.h    # Counter system interface

src/backend/commands/
└── blockchain_view.c       # System column management

src/backend/parser/
└── parse_relation.c        # Parser modifications (blockchain-specific)

src/backend/catalog/
└── blockchain_functions.sql # SQL helper functions
```

### Documentation

```
docs/
├── HLD_PostgreSQL_Blockchain_Extension.md    # High-level design
├── architecture/                             # Technical architecture
│   ├── blockchain-access-method.md
│   ├── hash-chain-system.md
│   ├── counter-management.md
│   └── immutability-enforcement.md
├── api/                                      # API references
│   ├── c-api-reference.md
│   └── sql-functions-reference.md
└── development/                              # Development guides
    ├── contributing.md
    ├── testing-guide.md
    └── debugging.md
```

## Contribution Guidelines

### Code Style and Standards

#### C Code Standards

1. **Formatting**: Follow PostgreSQL coding standards
   - Tabs for indentation (8-character tabs)
   - Line length: 80 characters maximum
   - K&R brace style

2. **Naming Conventions**:
   - Functions: `blockchain_function_name()`
   - Structs: `BlockchainStructName`
   - Constants: `BLOCKCHAIN_CONSTANT_NAME`
   - Static functions: `internal_function_name()`

3. **Header Guards**:
```c
#ifndef BLOCKCHAIN_MODULE_H
#define BLOCKCHAIN_MODULE_H
/* content */
#endif /* BLOCKCHAIN_MODULE_H */
```

4. **Function Documentation**:
```c
/*
 * Brief description of function purpose
 * 
 * Parameters:
 *   param1: Description of first parameter
 *   param2: Description of second parameter
 * 
 * Returns: Description of return value
 * 
 * Notes: Additional implementation notes
 */
static ReturnType
function_name(Type param1, Type param2)
{
    /* Implementation */
}
```

#### SQL Code Standards

1. **Keywords**: Uppercase SQL keywords
2. **Identifiers**: Lowercase with underscores
3. **Indentation**: 4 spaces for nested statements
4. **Comments**: Use `--` for single-line, `/* */` for multi-line

### Git Workflow

#### Branch Naming

- `feature/description` - New features
- `fix/issue-description` - Bug fixes  
- `refactor/component-name` - Refactoring
- `docs/section-name` - Documentation updates
- `test/test-description` - Test additions

#### Commit Messages

Follow conventional commit format:

```
type(scope): brief description

Detailed explanation of changes made and reasoning.

Fixes #123
```

Types:
- `feat`: New feature
- `fix`: Bug fix
- `refactor`: Code refactoring
- `docs`: Documentation changes
- `test`: Adding or modifying tests
- `perf`: Performance improvements

Example:
```
feat(counter): implement lazy recovery mechanism

Add lazy recovery system that recovers counter values from table data
on first access after server restart. This eliminates startup delays
and provides more robust recovery compared to file-based persistence.

The implementation uses MAX(__tx_lsn) queries to efficiently determine
the last used counter value for each blockchain table.

Fixes #45
```

### Testing Requirements

#### Mandatory Tests

All contributions must include appropriate tests:

1. **Unit Tests**: For individual functions
2. **Integration Tests**: For component interactions
3. **Regression Tests**: To prevent breaking existing functionality
4. **Performance Tests**: For performance-critical changes

#### Test File Organization

```
tests/
├── unit/
│   ├── test_hash_functions.sql
│   ├── test_counter_system.sql
│   └── test_system_columns.sql
├── integration/
│   ├── test_blockchain_tam.sql
│   ├── test_ddl_protection.sql
│   └── test_concurrent_operations.sql
├── performance/
│   ├── test_insert_performance.sql
│   ├── test_hash_performance.sql
│   └── test_counter_performance.sql
└── stress/
    ├── blockchain_stress_test.sql
    └── concurrent_stress_test.sh
```

#### Test Writing Guidelines

1. **Test Isolation**: Each test should be independent
2. **Cleanup**: Always clean up test resources
3. **Documentation**: Comment complex test scenarios
4. **Edge Cases**: Test boundary conditions and error cases

Example test structure:
```sql
-- Test: Basic blockchain table creation and insertion
\echo '=== Testing Basic Blockchain Operations ==='

-- Setup
CREATE BLOCKCHAIN TABLE test_basic (
    id INTEGER,
    data TEXT
);

-- Test insertion
INSERT INTO test_basic VALUES (1, 'test data');

-- Verify system columns are populated
SELECT 
    CASE WHEN __tx_lsn = 1 THEN 'PASS' ELSE 'FAIL' END as counter_test,
    CASE WHEN __curr_hash IS NOT NULL THEN 'PASS' ELSE 'FAIL' END as hash_test
FROM test_basic;

-- Test immutability
\echo 'Testing UPDATE prevention...'
\set ON_ERROR_STOP off
UPDATE test_basic SET data = 'modified';
\set ON_ERROR_STOP on

-- Cleanup
DROP TABLE test_basic;
```

## Development Process

### Feature Development Workflow

1. **Issue Creation**: Create GitHub issue describing the feature
2. **Design Discussion**: Discuss design in the issue
3. **Branch Creation**: Create feature branch
4. **Development**: Implement feature with tests
5. **Self Review**: Review your own code thoroughly
6. **Pull Request**: Create PR with detailed description
7. **Code Review**: Address reviewer feedback
8. **Testing**: Ensure all tests pass
9. **Merge**: Merge after approval

### Bug Fix Workflow

1. **Reproduce**: Create minimal reproduction case
2. **Root Cause**: Identify the underlying cause
3. **Fix Development**: Implement targeted fix
4. **Regression Test**: Add test to prevent recurrence
5. **Verification**: Verify fix works and doesn't break other functionality

### Code Review Process

#### For Authors

1. **Self-Review**: Review your own code before submitting
2. **Test Coverage**: Ensure adequate test coverage
3. **Documentation**: Update relevant documentation
4. **Performance**: Consider performance implications
5. **Backward Compatibility**: Maintain API compatibility

#### For Reviewers

Review areas to focus on:

1. **Correctness**: Does the code work as intended?
2. **Security**: Are there security implications?
3. **Performance**: Any performance regressions?
4. **Code Quality**: Follows project standards?
5. **Tests**: Adequate test coverage?
6. **Documentation**: Is documentation updated?

### Review Checklist

#### Blockchain-Specific Considerations

- [ ] Does not compromise immutability guarantees
- [ ] Maintains hash chain integrity
- [ ] Preserves counter system consistency  
- [ ] Handles concurrent access properly
- [ ] Includes appropriate error handling
- [ ] Updates system column handling if needed
- [ ] Considers integration with existing PostgreSQL features

#### General Code Quality

- [ ] Follows PostgreSQL coding standards
- [ ] Includes comprehensive tests
- [ ] Updates documentation
- [ ] Handles memory management properly
- [ ] Includes proper error messages
- [ ] Considers edge cases

## Architecture Guidelines

### Adding New Features

When adding new blockchain functionality:

1. **Design First**: Create design document
2. **Interface Design**: Define clean interfaces
3. **Integration Points**: Consider PostgreSQL integration
4. **Backward Compatibility**: Maintain existing APIs
5. **Performance Impact**: Measure performance impact
6. **Security Review**: Consider security implications

### Modifying Existing Components

When modifying existing code:

1. **Impact Analysis**: Understand full impact of changes
2. **Test Coverage**: Ensure existing tests still pass
3. **Performance**: Verify no performance regression
4. **API Compatibility**: Maintain interface compatibility
5. **Documentation**: Update relevant documentation

### Core Principles to Maintain

1. **Immutability**: Never compromise data immutability
2. **Integrity**: Maintain cryptographic hash chain integrity
3. **Performance**: Keep overhead minimal
4. **Simplicity**: Prefer simple, understandable solutions
5. **Reliability**: Ensure system remains stable

## Security Considerations

### Security Review Process

All contributions must consider:

1. **Data Protection**: Ensure immutability is preserved
2. **Access Control**: Respect PostgreSQL permission system
3. **Input Validation**: Validate all inputs thoroughly
4. **Error Handling**: Don't leak sensitive information
5. **Cryptographic Security**: Use proven cryptographic methods

### Common Security Issues

Avoid these common security issues:

1. **Buffer Overflows**: Always check buffer bounds
2. **SQL Injection**: Use parameterized queries
3. **Information Disclosure**: Don't leak sensitive data
4. **Privilege Escalation**: Respect permission boundaries
5. **Side Channels**: Consider timing attack implications

## Performance Guidelines

### Performance Testing

All performance-affecting changes require:

1. **Baseline Measurements**: Measure before changes
2. **Impact Testing**: Measure after changes  
3. **Regression Testing**: Ensure no unexpected slowdowns
4. **Scalability Testing**: Test with various data sizes
5. **Concurrency Testing**: Test under concurrent load

### Optimization Principles

1. **Measure First**: Profile before optimizing
2. **Optimize Hot Paths**: Focus on frequently executed code
3. **Memory Efficiency**: Minimize memory allocations
4. **Lock Contention**: Minimize lock holding time
5. **I/O Efficiency**: Minimize disk I/O operations

## Communication

### Getting Help

- **GitHub Issues**: For bugs and feature requests
- **GitHub Discussions**: For design discussions
- **Mailing Lists**: For broader PostgreSQL community discussions
- **Documentation**: Check existing documentation first

### Community Guidelines

1. **Be Respectful**: Treat all contributors with respect
2. **Be Constructive**: Provide helpful feedback
3. **Be Patient**: Allow time for review and response
4. **Be Clear**: Communicate clearly and concisely
5. **Be Collaborative**: Work together toward common goals

## Release Process

### Version Management

- **Major Versions**: Significant new features or breaking changes
- **Minor Versions**: New features with backward compatibility
- **Patch Versions**: Bug fixes and small improvements

### Release Checklist

Before each release:

- [ ] All tests pass
- [ ] Performance benchmarks run
- [ ] Documentation updated
- [ ] Security review completed
- [ ] Backward compatibility verified
- [ ] Release notes prepared

## Recognition

Contributors are recognized in:

- **CONTRIBUTORS.md**: List of all contributors
- **Release Notes**: Major contribution acknowledgments
- **Commit Messages**: Co-authored-by tags where appropriate

## License

By contributing, you agree that your contributions will be licensed under the same license as PostgreSQL (PostgreSQL License).

## Questions?

If you have questions about contributing:

1. Check the existing documentation
2. Search GitHub issues and discussions
3. Create a new discussion for design questions
4. Create an issue for specific bugs or feature requests

We welcome all contributions and appreciate your help in making the PostgreSQL Blockchain Extension better for everyone!