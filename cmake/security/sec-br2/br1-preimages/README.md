# Historical contract preimages

These two files are the unchanged Git blob bytes from SEC-BR1 commit
`d5ded1bfc0ee3bd44d0dcb11cd5f0bbccc8c35ed`.
They are data for reconstructing the historical BR1 contract view, not active
CMake entry points. The original BR1 ledger verifies their byte identities.

BR2 changes the product version, resource and CMake dispatch. It does not change
the old ledger or approve changed bytes under that ledger. Other historical
contract inputs remain unchanged and are copied from the current source.
`PrepareContracts` writes a fresh contract/test view without requiring Git
metadata in a Source ZIP. It refuses an existing destination.
